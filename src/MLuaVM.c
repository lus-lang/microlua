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
#if MLUA_ENABLE_COMPILER
#include "MLuaParse.h"
#endif
#include "MLuaString.h"
#include "MLuaUndump.h"

#define IS_BYTECODE(data, len)                                                 \
  ((len) >= 4 && (U8)(data)[0] == 0x1B && (data)[1] == 'M' &&                 \
   (data)[2] == 'L' && (data)[3] == 'u')

/* ========================================================================== */
/* VM Error Handling with Line Info                                           */
/* ========================================================================== */

/*
 * VM error handling. Used inside RunVM where proto, pc and baseFrame are in
 * scope. On error: capture line + stack trace while the frames are intact,
 * then unwind every frame this RunVM owns (closing upvalues per frame) and
 * return the status. The C caller (MLuaCall) restores its own registers.
 */
#define VM_FAIL(L, code, msg)                                                  \
  do {                                                                         \
    Size _pc = (Size)(pc - proto->Code);                                       \
    (L)->ErrorMsg = (msg);                                                     \
    (L)->ErrorLine = MLuaGetLine(proto, _pc);                                  \
    (L)->StackTrace = BuildStackTrace(L, proto, _pc);                          \
    UnwindFrames(L, baseFrame);                                                \
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
      UnwindFrames(L, baseFrame);                                              \
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
      UnwindFrames(L, baseFrame);                                              \
      return MLUA_ERR_RUNTIME;                                                 \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/* EvalStack Operations (Three-Array Architecture)                            */
/* ========================================================================== */

void MLuaPush(MLuaState *L, MLuaValue v) {
  /* Push to EvalStack */
  if (L->EvalTop >= L->EvalStackSize) {
    L->ErrorMsg = "stack overflow";
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

/* Get argument from the current frame's Args window (0-based) */
MLuaValue MLuaGetArg(MLuaState *L, int index) {
  if (index >= 0 && (Size)index < L->ArgsCount) {
    return L->Args[L->ArgsBase + (Size)index];
  }
  return MLUA_NIL;
}

int MLuaGetArgCount(MLuaState *L) { return (int)L->ArgsCount; }

/* Push result to EvalStack (for C function returns) */
void MLuaPushResult(MLuaState *L, MLuaValue v) { MLuaPush(L, v); }

/*
 * Stack access. Inside a light C function (InCCall), positive indices
 * address the call's arguments — Lua-style: index 1 is the first argument.
 * Outside any C call (embedding code), positive indices address the
 * EvalStack from its bottom. Negative indices always address the EvalStack
 * from its top.
 */
MLuaValue MLuaGetStack(MLuaState *L, int index) {
  if (index > 0) {
    if (L->InCCall) {
      return MLuaGetArg(L, index - 1);
    }
    if ((Size)index <= L->EvalTop) {
      return L->EvalStack[index - 1];
    }
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
    if (L->InCCall) {
      if ((Size)(index - 1) < L->ArgsCount) {
        L->Args[L->ArgsBase + (Size)(index - 1)] = v;
      }
      return;
    }
    if ((Size)index <= L->EvalTop) {
      L->EvalStack[index - 1] = v;
    }
  } else if (index < 0) {
    /* Negative: relative from EvalTop */
    int absIdx = (int)L->EvalTop + index;
    if (absIdx >= 0 && (Size)absIdx < L->EvalTop) {
      L->EvalStack[absIdx] = v;
    }
  }
}

int MLuaGetTop(MLuaState *L) {
  return L->InCCall ? (int)L->ArgsCount : (int)L->EvalTop;
}

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

/* Static buffer for stacktrace */
static char StackTraceBuffer[2048];

/*
 * Append formatted pieces to the stack-trace buffer without libc.
 */
static char *TraceAppend(char *p, char *end, const char *s, Size len) {
  while (len-- && p < end) {
    *p++ = *s++;
  }
  return p;
}

static char *TraceAppendLine(char *p, char *end, const char *src, Size srcLen,
                             Size line, const char *label) {
  char numBuf[24];
  Size numLen = MLuaIntToStr((I64)line, numBuf);

  p = TraceAppend(p, end, "\t", 1);
  p = TraceAppend(p, end, src, srcLen);
  p = TraceAppend(p, end, ":", 1);
  p = TraceAppend(p, end, numBuf, numLen);
  p = TraceAppend(p, end, ": ", 2);
  p = TraceAppend(p, end, label, StrLen(label));
  p = TraceAppend(p, end, "\n", 1);
  return p;
}

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
    const char *label = (L->FrameTop <= 1) ? "in main chunk" : "in function";

    /* Get source name if available */
    if (IsString(currentProto->Source)) {
      src = MLuaStringData(currentProto->Source);
      srcLen = MLuaStringLen(currentProto->Source);
    } else if (IsShortStr(currentProto->Source)) {
      src = MLuaStringData(currentProto->Source);
      srcLen = MLuaShortStrLen(currentProto->Source);
    }

    /* Format: "source:line: in function" */
    p = TraceAppendLine(p, end, src, srcLen, line, label);
  }

  /* Walk the caller frames (most recent first); the topmost frame is the
   * current one and was reported above with its precise PC */
  for (i = L->FrameTop > 0 ? L->FrameTop - 1 : 0; i > 0; i--) {
    MLuaFrame *f = &L->Frames[i - 1];
    MLuaProto *proto;
    Size line;
    const char *src = "?";
    Size srcLen = 1;
    const char *label = (i == 1) ? "in main chunk" : "in function";

    if (!IsPtr(f->Func)) {
      continue;
    }
    proto = MLUA_CLOSURE((MLuaGCHeader *)GetPtr(f->Func))->Proto;
    if (!proto) {
      continue;
    }

    line = MLuaGetLine(proto, f->PC);

    /* Get source name if available */
    if (IsString(proto->Source)) {
      src = MLuaStringData(proto->Source);
      srcLen = MLuaStringLen(proto->Source);
    } else if (IsShortStr(proto->Source)) {
      src = MLuaStringData(proto->Source);
      srcLen = MLuaShortStrLen(proto->Source);
    }

    p = TraceAppendLine(p, end, src, srcLen, line, label);
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
    I32 ia = MLuaGetIntVal(a);
    I32 ib = IsInt(b) ? MLuaGetIntVal(b) : 0;
    switch (op) {
    case OP_ADD:
      return MLuaMakeInt(L, ia + ib);
    case OP_SUB:
      return MLuaMakeInt(L, ia - ib);
    case OP_MUL:
      return MLuaMakeInt(L, ia * ib);
    case OP_IDIV:
      return ib != 0 ? MLuaMakeInt(L, ia / ib) : MakeInt(0);
    case OP_MOD:
      return ib != 0 ? MLuaMakeInt(L, ia % ib) : MakeInt(0);
    case OP_UNM:
      return MLuaMakeInt(L, -ia);
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
    I32 ia = MLuaGetIntVal(a);
    I32 ib = MLuaGetIntVal(b);
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

  /* Numeric equality crosses the int/float divide (5 == 5.0 in Lua) */
  if (op == OP_EQ || op == OP_NEQ) {
    Bool aNum = IsInt(a) || MLuaIsNumber(a);
    Bool bNum = IsInt(b) || MLuaIsNumber(b);
    if (aNum && bNum) {
      Bool eq = ToNumber(a) == ToNumber(b);
      return (op == OP_EQ) ? eq : !eq;
    }
    /* Value equality otherwise (strings are interned: pointer equality
     * is value equality; short strings compare by their packed bits) */
    return (op == OP_EQ) ? (a == b) : (a != b);
  }

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
    return MLuaMakeInt(L, (I32)MLuaStringLen(v));
  }
  if (IsShortStr(v)) {
    return MakeInt((I32)MLuaShortStrLen(v));
  }
  if (IsPtr(v)) {
    MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
    if (MLUA_OBJTYPE(h) == OBJTYPE_TABLE) {
      return MLuaMakeInt(L, (I32)MLuaTableLen(v));
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
  Bool allStrings = TRUE;
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
      allStrings = FALSE;
    } else {
      /* Invalid type for concatenation */
      L->ErrorMsg = IsNil(v) ? "attempt to concatenate a nil value"
                             : "attempt to concatenate a non-string value";
      return MLUA_NIL;
    }
  }

  /*
   * Common case: every operand is already a string. Build the interned result
   * directly from the operands (no temporary buffer, no second copy) instead
   * of materializing into a scratch buffer and re-hashing via MLuaStringNew.
   * The number-bearing case still needs the scratch buffer to convert digits.
   */
  if (allStrings) {
    return MLuaStringConcatMany(L, &L->EvalStack[L->EvalTop - count], count);
  }

  /* Allocate buffer */
  buffer = (U8 *)MLuaAlloc(L, totalLen + 1);
  if (!buffer) {
    /* The nil sentinel only raises when ErrorMsg is set; a bare nil would
     * flow onward as a value (e.g. silently deleting the target of
     * `t[#t+1] = a .. b`). */
    L->ErrorMsg = "out of memory";
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
      const char *s = MLuaStringData(v);
      Size len = MLuaShortStrLen(v);
      MemCpy(p, s, len);
      p += len;
    } else if (IsInt(v)) {
      /* Simple integer to string */
      I32 n = MLuaGetIntVal(v);
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

#if MLUA_PROFILE_OPS
static U32 OpProfileCounts[256];

void MLuaDumpOpProfile(MLuaState *L) {
  int i;
  if (!L->OutputFunc) {
    return;
  }
  for (i = 0; i < 256; i++) {
    char buf[64];
    Size pos = 0;
    const char *name;
    Size nameLen;
    if (OpProfileCounts[i] == 0) {
      continue;
    }
    name = MLuaOpName((MLuaOpCode)i);
    nameLen = StrLen(name);
    MemCpy(buf, name, nameLen);
    pos = nameLen;
    buf[pos++] = '\t';
    pos += MLuaIntToStr((I64)OpProfileCounts[i], buf + pos);
    buf[pos++] = '\n';
    L->OutputFunc(L, MLUA_OUTPUT_PRINT, buf, pos);
  }
}
#endif

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

/* ========================================================================== */
/* Frame Management                                                           */
/* ========================================================================== */

/*
 * Push a Lua call frame. The callable Lua closure sits at EvalStack[funcPos]
 * with its nargs arguments above it. On success the func+args are popped,
 * the args copied into a fresh Args window, locals initialized at LocalsTop,
 * and L's frame registers switched to the new frame. ErrorMsg is set on
 * failure.
 */
static MLuaStatus PushFrame(MLuaState *L, Size funcPos, Size nargs) {
  MLuaValue func = L->EvalStack[funcPos];
  MLuaClosure *cl = MLUA_CLOSURE((MLuaGCHeader *)GetPtr(func));
  MLuaProto *proto = cl->Proto;
  MLuaFrame *f;
  Size newBase;
  Size win;
  Size i;

  if (L->FrameTop >= L->FrameCap) {
    L->ErrorMsg = "call stack overflow";
    return MLUA_ERRRUN;
  }

  if (funcPos + proto->MaxStackSize > L->EvalStackSize) {
    L->ErrorMsg = "stack overflow";
    return MLUA_ERRRUN;
  }

  newBase = L->LocalsTop;
  if (newBase + proto->NumLocals >= L->LocalsSize) {
    L->ErrorMsg = "stack overflow (too many locals)";
    return MLUA_ERRRUN;
  }

  win = L->ArgsTop;
  if (win + nargs > L->ArgsSize) {
    L->ErrorMsg = "argument stack overflow";
    return MLUA_ERRRUN;
  }

  /* Copy arguments into this frame's window */
  for (i = 0; i < nargs; i++) {
    L->Args[win + i] = L->EvalStack[funcPos + 1 + i];
  }

  f = &L->Frames[L->FrameTop++];
  f->Func = func;
  f->PC = 0;
  f->LocalsBase = newBase;
  f->EvalBase = funcPos;
  f->ArgsBase = win;
  f->ArgsCount = nargs;

  /* Pop func+args; the callee's results will land at funcPos */
  L->EvalTop = funcPos;

  /* Initialize locals: parameters first, then nil */
  for (i = 0; i < proto->NumLocals; i++) {
    L->Locals[newBase + i] = (i < nargs) ? L->Args[win + i] : MLUA_NIL;
  }

  L->LocalsBase = newBase;
  L->LocalsTop = newBase + proto->NumLocals;
  L->ArgsBase = win;
  L->ArgsCount = nargs;
  L->ArgsTop = win + nargs;

  return MLUA_OK;
}

/*
 * Pop the top frame: close upvalues pointing into it, release its Args
 * window and Locals span, and restore the caller frame's registers (when
 * one exists within the current context; at a C boundary the caller
 * restores its own snapshot).
 */
static void PopFrame(MLuaState *L) {
  MLuaFrame *f = &L->Frames[L->FrameTop - 1];

  if (L->OpenUpvalues) {
    MLuaCloseUpvalues(L, &L->Locals[f->LocalsBase]);
  }

  L->ArgsTop = f->ArgsBase;
  L->LocalsTop = f->LocalsBase;
  L->FrameTop--;

  if (L->FrameTop > 0) {
    MLuaFrame *pf = &L->Frames[L->FrameTop - 1];
    L->LocalsBase = pf->LocalsBase;
    L->ArgsBase = pf->ArgsBase;
    L->ArgsCount = pf->ArgsCount;
  }
}

/* Unwind every frame above baseFrame (error path) */
static void UnwindFrames(MLuaState *L, Size baseFrame) {
  while (L->FrameTop > baseFrame) {
    PopFrame(L);
  }
}

/* ========================================================================== */
/* The Dispatch Loop                                                          */
/* ========================================================================== */

/*
 * Run the VM until the frame stack returns to baseFrame (the function whose
 * frame is at baseFrame has returned), an error unwinds to it, or a yield
 * suspends execution (frames are left intact for resume).
 *
 * The loop is frame-iterative: OP_CALL of a Lua closure pushes a frame and
 * continues in the same C invocation. C recursion only happens at genuine
 * C boundaries (pcall, require, library callbacks via MLuaCall) — which is
 * exactly where yielding is forbidden.
 */
static MLuaStatus RunVM(MLuaState *L, Size baseFrame) {
  const U8 *pc;
  MLuaProto *proto;
  MLuaClosure *cl;

  /* Load (or reload after resume) the top frame's execution registers */
#define RELOAD_FRAME()                                                         \
  do {                                                                         \
    MLuaFrame *_f = &L->Frames[L->FrameTop - 1];                               \
    cl = MLUA_CLOSURE((MLuaGCHeader *)GetPtr(_f->Func));                       \
    proto = cl->Proto;                                                         \
    pc = proto->Code + _f->PC;                                                 \
    L->LocalsBase = _f->LocalsBase;                                            \
    L->ArgsBase = _f->ArgsBase;                                                \
    L->ArgsCount = _f->ArgsCount;                                              \
  } while (0)

  RELOAD_FRAME();

  for (;;) {
    U8 op;

    /*
     * GC safepoint. Allocations never collect directly (they only set
     * GCPending), so C library code and the parser never see objects move
     * mid-operation. Here, between instructions, every live pointer is
     * reloadable from the frames: save the PC offset, collect, reload.
     * (pc itself stays valid — code buffers are pinned — but proto/cl move.)
     */
    if (L->GCPending) {
      L->GCPending = FALSE;
      L->Frames[L->FrameTop - 1].PC = (Size)(pc - proto->Code);
      MLuaGCCollect(L);
      RELOAD_FRAME();
    }

    op = READ_BYTE();

#if MLUA_PROFILE_OPS
    OpProfileCounts[op]++;
#endif

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

    case OP_GETLOCAL_CLEAR: {
      U8 slot = READ_BYTE();
      MLuaValue val = LOCAL_GET(slot);
      LOCAL_SET(slot, MLUA_NIL);
      STACK_PUSH(val);
      break;
    }

    case OP_SETLOCAL: {
      U8 slot = READ_BYTE();
      MLuaValue val = STACK_POP();
      LOCAL_SET(slot, val);
      break;
    }

    case OP_CLEARLOCAL: {
      U8 slot = READ_BYTE();
      LOCAL_SET(slot, MLUA_NIL);
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

    case OP_GETGLOBAL_K: {
      /* Fused LOADK B; GETGLOBAL */
      U8 k = READ_BYTE();
      MLuaValue val = MLuaTableGet(L, L->Globals, proto->Constants[k]);
      STACK_PUSH(val);
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
      /*
       * Fast path: inline-int key hitting a live array slot -- the hot
       * shape of every indexed loop. Preconditions mirror the safe chain
       * exactly: a nil slot must fall through (MLuaTableRawGet consults
       * the hash part and then the Forward chain for it), and boxed-int
       * keys (32-bit) take the generic route.
       */
      if (IsTable(tbl) && IsInlineInt(key)) {
        I32 i = GetInt(key);
        MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
        if (i >= 1 && (U32)i <= th->ArrayLen) {
          MLuaValue v = MLuaTableArrayData(th)[i - 1];
          if (!IsNil(v)) {
            STACK_PUSH(v);
            break;
          }
        }
      }
      VM_TRY(MLuaTableGetSafe(L, tbl, key, &result));
      STACK_PUSH(result);
      break;
    }

    case OP_SETTABLE: {
      MLuaValue val = STACK_POP();
      MLuaValue key = STACK_POP();
      MLuaValue tbl = STACK_TOP();
      /*
       * Fast path: non-nil store to an existing array slot. Appends
       * (i == len+1), nil stores (length bookkeeping), growth, and hole
       * errors all take the safe route.
       */
      if (IsTable(tbl) && IsInlineInt(key) && !IsNil(val)) {
        I32 i = GetInt(key);
        MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
        if (i >= 1 && (U32)i <= th->ArrayLen) {
          MLuaTableArrayData(th)[i - 1] = val;
          break;
        }
      }
      VM_TRY(MLuaTableSetSafe(L, tbl, key, val));
      break;
    }

    case OP_APPEND: {
      MLuaValue val = STACK_POP();
      MLuaValue tbl = STACK_TOP();
      MLuaTableAppend(L, tbl, val);
      break;
    }

    case OP_GETTABLE_LL: {
      /* Fused GETLOCAL t; GETLOCAL k; GETTABLE. Operand: (t << 4) | k. */
      U8 slots = READ_BYTE();
      MLuaValue tbl = LOCAL_GET(slots >> 4);
      MLuaValue key = LOCAL_GET(slots & 0x0F);
      MLuaValue result;
      if (IsTable(tbl) && IsInlineInt(key)) {
        I32 i = GetInt(key);
        MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
        if (i >= 1 && (U32)i <= th->ArrayLen) {
          MLuaValue v = MLuaTableArrayData(th)[i - 1];
          if (!IsNil(v)) {
            STACK_PUSH(v);
            break;
          }
        }
      }
      VM_TRY(MLuaTableGetSafe(L, tbl, key, &result));
      STACK_PUSH(result);
      break;
    }

    case OP_SETTABLE_LL: {
      /* Fused GETLOCAL t; GETLOCAL k; <value>; SETTABLE; POP 1: pops only
       * the value, the table never crosses the stack. */
      U8 slots = READ_BYTE();
      MLuaValue val = STACK_POP();
      MLuaValue tbl = LOCAL_GET(slots >> 4);
      MLuaValue key = LOCAL_GET(slots & 0x0F);
      if (IsTable(tbl) && IsInlineInt(key) && !IsNil(val)) {
        I32 i = GetInt(key);
        MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
        if (i >= 1 && (U32)i <= th->ArrayLen) {
          MLuaTableArrayData(th)[i - 1] = val;
          break;
        }
      }
      VM_TRY(MLuaTableSetSafe(L, tbl, key, val));
      break;
    }

    case OP_SETTABLE_POP: {
      /* SETTABLE that also drops the table (fuses the trailing POP 1) */
      MLuaValue val = STACK_POP();
      MLuaValue key = STACK_POP();
      MLuaValue tbl = STACK_POP();
      if (IsTable(tbl) && IsInlineInt(key) && !IsNil(val)) {
        I32 i = GetInt(key);
        MLuaTableHeader *th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tbl));
        if (i >= 1 && (U32)i <= th->ArrayLen) {
          MLuaTableArrayData(th)[i - 1] = val;
          break;
        }
      }
      VM_TRY(MLuaTableSetSafe(L, tbl, key, val));
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
      /* Inline ints are canonical: bitwise equality is value equality */
      if (IsInlineInt(a) && IsInlineInt(b)) {
        STACK_PUSH(a == b ? MLUA_TRUE : MLUA_FALSE);
        break;
      }
      STACK_PUSH(MLuaCompare(L, OP_EQ, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_NEQ: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      if (IsInlineInt(a) && IsInlineInt(b)) {
        STACK_PUSH(a != b ? MLUA_TRUE : MLUA_FALSE);
        break;
      }
      STACK_PUSH(MLuaCompare(L, OP_NEQ, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_LT: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      if (IsInlineInt(a) && IsInlineInt(b)) {
        STACK_PUSH(GetInt(a) < GetInt(b) ? MLUA_TRUE : MLUA_FALSE);
        break;
      }
      STACK_PUSH(MLuaCompare(L, OP_LT, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_LE: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      if (IsInlineInt(a) && IsInlineInt(b)) {
        STACK_PUSH(GetInt(a) <= GetInt(b) ? MLUA_TRUE : MLUA_FALSE);
        break;
      }
      STACK_PUSH(MLuaCompare(L, OP_LE, a, b) ? MLUA_TRUE : MLUA_FALSE);
      break;
    }

    case OP_ADD:
    case OP_SUB:
    case OP_MUL: {
      MLuaValue b = STACK_POP();
      MLuaValue a = STACK_POP();
      MLuaValue result;
      /*
       * Fast path: two inline ints whose result stays inline. The
       * expressions match MLuaArith's integer path exactly; a result
       * outside the inline range falls through so the generic path does
       * the boxing (the range test folds to always-true on the 64-bit
       * representation, where every I32 is inline).
       */
      if (IsInlineInt(a) && IsInlineInt(b)) {
        I32 ia = GetInt(a);
        I32 ib = GetInt(b);
        I32 r = (op == OP_ADD)   ? ia + ib
                : (op == OP_SUB) ? ia - ib
                                 : ia * ib;
        if (r >= MLUA_INLINE_INT_MIN && r <= MLUA_INLINE_INT_MAX) {
          STACK_PUSH(MakeInt(r));
          break;
        }
      }
      result = MLuaArith(L, (MLuaOpCode)op, a, b);
      VM_CHECK_NIL(result); /* Type error in arithmetic */
      STACK_PUSH(result);
      break;
    }

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

      I32 start = MLuaGetIntVal(startVal);
      I32 limit = MLuaGetIntVal(limitVal);
      I32 step = MLuaGetIntVal(stepVal);
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
      I32 idx = MLuaGetIntVal(LOCAL_GET(base));
      I32 limit = MLuaGetIntVal(LOCAL_GET(base + 1));
      I32 step = MLuaGetIntVal(LOCAL_GET(base + 2));
      I32 bodyPC = GetInt(LOCAL_GET(base + 3)); /* Stored body target PC */

      /* Increment index (one boxing per step; the same value feeds the
       * local and, when the loop continues, the pushed index) */
      idx += step;
      {
        MLuaValue nv = MLuaMakeInt(L, idx);
        VM_CHECK_NIL(nv); /* 32-bit boxing can hit out-of-memory */
        LOCAL_SET(base, nv);

        /* Boundary check */
        if ((step > 0 && idx <= limit) || (step < 0 && idx >= limit)) {
          /* Continue loop - push new index, jump to body */
          STACK_PUSH(nv);
          pc = proto->Code + bodyPC;
        }
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

    case OP_CALL:
    case OP_CALLM:
    case OP_TAILCALL: {
      /*
       * Function call. Stack: [..., func, arg1 .. argN].
       * OP_CALL:     N = operand.
       * OP_CALLM:    N = operand fixed args + the last call's results
       *              (supports f(g()) and f(...) passing everything).
       * OP_TAILCALL: like OP_CALL but the current frame is replaced, so
       *              tail-recursive Lua code runs in constant frame space.
       */
      U8 operand = READ_BYTE();
      Size nargs_call = (op == OP_CALLM)
                            ? (Size)operand + L->LastCallResults
                            : (Size)operand;
      Size funcPos = L->EvalTop - nargs_call - 1;
      MLuaValue func = L->EvalStack[funcPos];

      if (IsLightFunc(func)) {
        /* Light C function: give it a fresh Args window */
        Size funcIdx = GetLightFuncIdx(func);
        if (funcIdx >= L->LightFuncCount || !L->LightFuncs) {
          VM_FAIL(L, MLUA_ERR_RUNTIME, "attempt to call an invalid function");
        }

        {
          MLuaCFunction cfunc = (MLuaCFunction)L->LightFuncs[funcIdx];
          Size win = L->ArgsTop;
          Bool savedInC;
          int results;
          Size i;

          if (win + nargs_call > L->ArgsSize) {
            VM_FAIL(L, MLUA_ERR_RUNTIME, "argument stack overflow");
          }
          for (i = 0; i < nargs_call; i++) {
            L->Args[win + i] = L->EvalStack[funcPos + 1 + i];
          }
          L->EvalTop = funcPos;
          L->ArgsBase = win;
          L->ArgsCount = nargs_call;
          L->ArgsTop = win + nargs_call;

          L->Frames[L->FrameTop - 1].PC = (Size)(pc - proto->Code);
          savedInC = L->InCCall;
          L->InCCall = TRUE;
          results = cfunc(L);
          L->InCCall = savedInC;

          /* Yield requested (only coroutine.yield sets this): save the
           * resume point and suspend. The yield args stay reserved in
           * their Args window for the resumer to collect. */
          if (L->YieldFlag) {
            L->YieldFlag = FALSE;
            L->Frames[L->FrameTop - 1].PC = (Size)(pc - proto->Code);
            {
              MLuaFrame *cf = &L->Frames[L->FrameTop - 1];
              L->ArgsBase = cf->ArgsBase;
              L->ArgsCount = cf->ArgsCount;
            }
            return MLUA_YIELD;
          }

          /* The C function may have called back into Lua and triggered a
           * compacting GC. Re-derive all frame-local raw pointers before
           * touching proto/pc/cl again. */
          RELOAD_FRAME();

          /* Release the C call's window, restore this frame's */
          L->ArgsTop = win;
          {
            MLuaFrame *cf = &L->Frames[L->FrameTop - 1];
            L->ArgsBase = cf->ArgsBase;
            L->ArgsCount = cf->ArgsCount;
          }

          if (results < 0) {
            /* C function signaled error - L->ErrorMsg should be set */
            VM_TRY(MLUA_ERRRUN);
          }
          if (L->EvalTop - funcPos < (Size)results) {
            VM_FAIL(L, MLUA_ERR_RUNTIME, "stack overflow");
          }
          if (results == 0) {
            STACK_PUSH(MLUA_NIL);
          }
          L->LastCallResults = (results == 0) ? 1 : (Size)results;

          /* A tail call to a C function is call + immediate return */
          if (op == OP_TAILCALL) {
            goto do_return;
          }
        }
        break;
      }

      if (!IsPtr(func) ||
          MLUA_OBJTYPE((MLuaGCHeader *)GetPtr(func)) != OBJTYPE_FUNCTION ||
          !MLUA_CLOSURE((MLuaGCHeader *)GetPtr(func))->Proto) {
        VM_FAIL(L, MLUA_ERR_RUNTIME,
                IsNil(func) ? "attempt to call a nil value"
                            : "attempt to call a non-function value");
      }

      /* Lua closure: push (or replace, for tail calls) a frame and keep
       * dispatching — no C recursion. */
      if (op == OP_TAILCALL && L->FrameTop > baseFrame) {
        /* Replace the current frame: free its locals/window and slide the
         * func+args down to where our results were expected. */
        MLuaFrame dying = L->Frames[L->FrameTop - 1];
        Size span = nargs_call + 1;
        Size i;

        if (L->OpenUpvalues) {
          MLuaCloseUpvalues(L, &L->Locals[dying.LocalsBase]);
        }
        L->FrameTop--;
        L->ArgsTop = dying.ArgsBase;
        L->LocalsTop = dying.LocalsBase;

        if (funcPos != dying.EvalBase) {
          for (i = 0; i < span; i++) {
            L->EvalStack[dying.EvalBase + i] = L->EvalStack[funcPos + i];
          }
        }
        L->EvalTop = dying.EvalBase + span;
        funcPos = dying.EvalBase;
      } else {
        /* Save the return point in the calling frame */
        L->Frames[L->FrameTop - 1].PC = (Size)(pc - proto->Code);
      }

      {
        MLuaStatus st = PushFrame(L, funcPos, nargs_call);
        if (st != MLUA_OK) {
          VM_TRY(st);
        }
      }
      RELOAD_FRAME();
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
       * Push varargs from this frame's Args window.
       * Varargs are Args[ArgsBase + NumParams .. ArgsBase + ArgsCount).
       * want > 0: push exactly that many (nil-padded).
       * want == 0: push ALL varargs and set LastCallResults, so the
       *            multi-result machinery (ADJUST/CALLM/APPENDM) applies.
       */
      U8 want = READ_BYTE();
      int varargCount = (int)L->ArgsCount - (int)proto->NumParams;
      Size varargStart = L->ArgsBase + proto->NumParams;
      Size i;

      if (varargCount < 0) {
        varargCount = 0;
      }

      if (want == 0) {
        for (i = 0; i < (Size)varargCount; i++) {
          STACK_PUSH(L->Args[varargStart + i]);
        }
        L->LastCallResults = (Size)varargCount;
      } else {
        for (i = 0; i < want; i++) {
          if ((int)i < varargCount) {
            STACK_PUSH(L->Args[varargStart + i]);
          } else {
            STACK_PUSH(MLUA_NIL);
          }
        }
      }
      break;
    }

    case OP_ADJUST: {
      /* Normalize the last call's results to exactly 'want' values */
      U8 want = READ_BYTE();
      Size have = L->LastCallResults;

      while (have > want && L->EvalTop > 0) {
        L->EvalTop--;
        have--;
      }
      while (have < want) {
        STACK_PUSH(MLUA_NIL);
        have++;
      }
      L->LastCallResults = want;
      break;
    }

    case OP_APPENDM: {
      /* Append the last call's results to the table beneath them */
      Size n = L->LastCallResults;
      MLuaValue tbl = L->EvalStack[L->EvalTop - n - 1];
      Size i;

      for (i = 0; i < n; i++) {
        MLuaTableAppend(L, tbl, L->EvalStack[L->EvalTop - n + i]);
      }
      L->EvalTop -= n;
      break;
    }

    case OP_RET:
    case OP_RET0:
    case OP_RET1: {
      U8 nret;

      if (op == OP_RET) {
        nret = READ_BYTE();
      } else {
        nret = (op == OP_RET1) ? 1 : 0;
      }

      /*
       * Return values are already on the EvalStack above this frame's
       * EvalBase. If nothing was returned, push nil (Lua convention:
       * every call yields at least one value).
       */
      if (nret == 0) {
        STACK_PUSH(MLUA_NIL);
      }

    do_return : {
      MLuaFrame *f = &L->Frames[L->FrameTop - 1];
      L->LastCallResults = L->EvalTop - f->EvalBase;

      PopFrame(L);

      if (L->FrameTop == baseFrame) {
        /* This RunVM invocation's work is done; the C caller restores
         * its own register snapshot. */
        return MLUA_OK;
      }

      RELOAD_FRAME();
      break;
    }
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

  if (L->EvalTop < (Size)(nargs + 1)) {
    L->ErrorMsg = "not enough values on the stack";
    return MLUA_ERRRUN;
  }

  funcPos = L->EvalTop - nargs - 1;
  func = L->EvalStack[funcPos];

  if (IsLightFunc(func)) {
    /* Light C function called from the C boundary */
    Size funcIdx = GetLightFuncIdx(func);
    if (funcIdx < L->LightFuncCount && L->LightFuncs) {
      MLuaCFunction cfunc = (MLuaCFunction)L->LightFuncs[funcIdx];
      Size win = L->ArgsTop;
      Size savedArgsBase = L->ArgsBase;
      Size savedArgsCount = L->ArgsCount;
      Bool savedInC = L->InCCall;
      int results;
      int i;

      if (win + (Size)nargs > L->ArgsSize) {
        L->ErrorMsg = "argument stack overflow";
        return MLUA_ERRRUN;
      }
      for (i = 0; i < nargs; i++) {
        L->Args[win + (Size)i] = L->EvalStack[funcPos + 1 + (Size)i];
      }
      L->EvalTop = funcPos;
      L->ArgsBase = win;
      L->ArgsCount = (Size)nargs;
      L->ArgsTop = win + (Size)nargs;

      L->InCCall = TRUE;
      results = cfunc(L);
      L->InCCall = savedInC;

      L->ArgsTop = win;
      L->ArgsBase = savedArgsBase;
      L->ArgsCount = savedArgsCount;

      if (results < 0) {
        return MLUA_ERRRUN; /* ErrorMsg set by the C function */
      }
      if (L->EvalTop - funcPos < (Size)results) {
        L->ErrorMsg = "stack overflow";
        return MLUA_ERRRUN;
      }
      if (results == 0) {
        MLuaPush(L, MLUA_NIL);
      }
      L->LastCallResults = (results == 0) ? 1 : (Size)results;
      return MLUA_OK;
    }
    L->ErrorMsg = "attempt to call an invalid function";
    return MLUA_ERRRUN;
  }

  if (IsPtr(func) &&
      MLUA_OBJTYPE((MLuaGCHeader *)GetPtr(func)) == OBJTYPE_FUNCTION &&
      MLUA_CLOSURE((MLuaGCHeader *)GetPtr(func))->Proto) {
    /* Lua closure: enter the frame machine. This is the C boundary —
     * yields cannot cross it. */
    Size savedLocalsBase = L->LocalsBase;
    Size savedArgsBase = L->ArgsBase;
    Size savedArgsCount = L->ArgsCount;
    Bool savedInC = L->InCCall;
    Size evalBase = funcPos;
    MLuaStatus st;

    L->InCCall = FALSE; /* Lua code is running, not C */
    L->CCallDepth++;

    st = PushFrame(L, funcPos, (Size)nargs);
    if (st == MLUA_OK) {
      st = RunVM(L, L->FrameTop - 1);
    }

    L->CCallDepth--;
    L->InCCall = savedInC;

    if (st == MLUA_OK) {
      L->LastCallResults = L->EvalTop - evalBase;
    }

    /* On MLUA_YIELD the frames stay suspended (the thread context snapshot
     * picks them up); the register restore below is still correct because
     * RunVM reloads them from the frame on resume. */
    L->LocalsBase = savedLocalsBase;
    L->ArgsBase = savedArgsBase;
    L->ArgsCount = savedArgsCount;

    return st;
  }

  L->ErrorMsg = IsNil(func) ? "attempt to call a nil value"
                            : "attempt to call a non-function value";
  return MLUA_ERRRUN;
}

/*
 * Re-enter the dispatch loop for a suspended thread context whose frames
 * are already in place (resume-after-yield). Runs until frame 0 returns.
 */
MLuaStatus MLuaRunSuspended(MLuaState *L) {
  if (L->FrameTop == 0) {
    L->ErrorMsg = "no suspended frames to resume";
    return MLUA_ERRRUN;
  }
  return RunVM(L, 0);
}

/*
 * Legacy execution entry: run a closure with nargs arguments pre-loaded in
 * the current Args window (Args[ArgsBase..ArgsBase+nargs)). Kept for the
 * embedding API and internal tests; new code should push func+args on the
 * EvalStack and use MLuaCall.
 */
MLuaStatus MLuaExecute(MLuaState *L, MLuaClosure *cl, int nargs,
                       int nresults) {
  int i;

  if (!cl || !cl->Proto) {
    L->ErrorMsg = "attempt to execute an invalid closure";
    return MLUA_ERRRUN;
  }

  MLuaPush(L, MakePtr(cl));
  for (i = 0; i < nargs; i++) {
    MLuaPush(L, MLuaGetArg(L, i));
  }

  return MLuaCall(L, nargs, nresults);
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

#if MLUA_ENABLE_COMPILER
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
#endif

MLuaStatus MLuaLoadBytecode(MLuaState *L, const char *data, Size len,
                            const char *name) {
  MLuaProto *proto;
  MLuaClosure *cl;

  UNUSED(name);

  proto = MLuaUndump(L, data, len);
  if (!proto) {
    const char *msg = L->ErrorMsg ? L->ErrorMsg : "bytecode load error";
    MLuaPush(L, MLuaStringNew(L, msg, StrLen(msg)));
    return MLUA_ERRRUN;
  }

  cl = MLuaClosureNew(L, proto, 0);
  if (!cl) {
    MLuaPush(L, MLuaStringNew(L, "memory error", 12));
    return MLUA_ERRMEM;
  }

  MLuaPush(L, MakePtr(cl));
  return MLUA_OK;
}

MLuaStatus MLuaDoBytecode(MLuaState *L, const char *data, Size len,
                          const char *name) {
  MLuaStatus status;
  MLuaValue func;
  MLuaClosure *cl;

  status = MLuaLoadBytecode(L, data, len, name);
  if (status != MLUA_OK) {
    return status;
  }
  func = MLuaPop(L);
  cl = MLUA_CLOSURE((MLuaGCHeader *)GetPtr(func));
  return MLuaExecute(L, cl, 0, 0);
}

MLuaStatus MLuaLoadBuffer(MLuaState *L, const char *data, Size len,
                          const char *name) {
  if (IS_BYTECODE(data, len)) {
    return MLuaLoadBytecode(L, data, len, name);
  }
#if MLUA_ENABLE_COMPILER
  return MLuaLoadString(L, data, len, name);
#else
  UNUSED(name);
  L->ErrorMsg = "source compiler disabled";
  MLuaPush(L, MLuaStringNew(L, L->ErrorMsg, StrLen(L->ErrorMsg)));
  return MLUA_ERRRUN;
#endif
}

MLuaStatus MLuaDoBuffer(MLuaState *L, const char *data, Size len,
                        const char *name) {
  if (IS_BYTECODE(data, len)) {
    return MLuaDoBytecode(L, data, len, name);
  }
#if MLUA_ENABLE_COMPILER
  return MLuaDoString(L, data, len, name);
#else
  UNUSED(name);
  L->ErrorMsg = "source compiler disabled";
  return MLUA_ERRRUN;
#endif
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
