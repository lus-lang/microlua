/*
 * MicroLua - MLuaParse.c
 * Pratt parser with stack-based codegen
 */

#include "MLuaParse.h"
#include "MLuaString.h"

/* ========================================================================== */
/* Line Info Helper                                                           */
/* ========================================================================== */

/*
 * Helper to emit line info before emitting bytecode.
 * Call at the start of each statement or expression parsing.
 */
#define EMIT_LINE(p) MLuaEmitLine((p)->FS, (p)->Lex.Line)

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

static void ParseChunk(MLuaParser *p);
static void ParseBlock(MLuaParser *p);
static void ParseStatement(MLuaParser *p);
static void ParseExpr(MLuaParser *p);
static int ParseFuncBody(MLuaParser *p, Bool isMethod);

/* ========================================================================== */
/* Parser Helpers                                                             */
/* ========================================================================== */

/* Port-overridable: bounds the parser's C-stack recursion, so targets with a
 * small C stack must lower it (each level is roughly a ParseExpr frame). */
#ifndef MLUA_PARSE_MAX_DEPTH
#define MLUA_PARSE_MAX_DEPTH 256
#endif

static void Error(MLuaParser *p, const char *msg) {
  if (!p->Error) {
    p->Error = msg;
    p->ErrorLine = p->Lex.Line;
  }
}

static Bool EnterParseDepth(MLuaParser *p) {
  if (p->ParseDepth >= MLUA_PARSE_MAX_DEPTH) {
    Error(p, "source nesting too deep");
    return FALSE;
  }
  p->ParseDepth++;
  return TRUE;
}

static void LeaveParseDepth(MLuaParser *p) {
  if (p->ParseDepth > 0) {
    p->ParseDepth--;
  }
}

static Bool Check(MLuaParser *p, MLuaTokenType t) {
  return p->Lex.Token.Type == t;
}

static void Advance(MLuaParser *p) {
  MLuaLexNext(&p->Lex);
  if (p->Lex.Error) {
    Error(p, p->Lex.Error);
  }
}

static Bool Match(MLuaParser *p, MLuaTokenType t) {
  if (Check(p, t)) {
    Advance(p);
    return TRUE;
  }
  return FALSE;
}

static void Expect(MLuaParser *p, MLuaTokenType t) {
  if (!Check(p, t)) {
    Error(p, "unexpected token");
    return;
  }
  Advance(p);
}

/* ========================================================================== */
/* Stack Tracking                                                             */
/* ========================================================================== */

static void StackPush(MLuaParser *p, int count) {
  p->FS->StackLevel += count;
  if (p->FS->StackLevel > p->FS->MaxStack) {
    p->FS->MaxStack = p->FS->StackLevel;
  }
}

static void StackPop(MLuaParser *p, int count) { p->FS->StackLevel -= count; }

/* ========================================================================== */
/* Local Variables                                                            */
/* ========================================================================== */

static int FindLocal(MLuaFuncState *fs, const char *name, Size len) {
  int i;

  for (i = (int)fs->LocalsSize - 1; i >= 0; i--) {
    MLuaValue v = fs->Locals[i].Name;
    const char *s;
    Size slen;

    /* Handle both short strings and long strings */
    if (IsShortStr(v)) {
      slen = MLuaShortStrLen(v);
      s = MLuaStringData(v); /* Uses static buffer for short strings */
    } else if (IsString(v)) {
      s = MLuaStringData(v);
      slen = MLuaStringLen(v);
    } else {
      continue;
    }

    if (slen == len && s && MemCmp(s, name, len) == 0) {
      return fs->Locals[i].Slot;
    }
  }

  return -1; /* Not found */
}

static void AddLocal(MLuaParser *p, const char *name, Size len) {
  MLuaFuncState *fs = p->FS;

  /* Slots are addressed by u8 operands; 255 is the hard cap (like Lua's
   * MAXVARS). */
  if (fs->NumLocals >= 255) {
    Error(p, "too many local variables (max 255)");
    return;
  }

  /* Grow locals array if needed */
  if (fs->LocalsSize >= fs->LocalsCap) {
    Size newCap = (fs->LocalsCap == 0) ? 8 : fs->LocalsCap * 2;
    /* IMPORTANT: Use actual struct element size, not sum of fields */
    /* The struct has padding for alignment! */
    Size elemSize = sizeof(fs->Locals[0]);
    Size allocSize = newCap * elemSize;
    void *mem = MLuaAlloc(p->L, allocSize);
    if (!mem) {
      Error(p, "out of memory");
      return;
    }
    /* Simple realloc simulation */
    if (fs->Locals) {
      Size copySize = fs->LocalsSize * elemSize;
      MemCpy(mem, fs->Locals, copySize);
    }
    fs->Locals = mem;
    fs->LocalsCap = newCap;
  }

  fs->Locals[fs->LocalsSize].Name = MLuaStringNew(p->L, name, len);
  fs->Locals[fs->LocalsSize].Slot = fs->NumLocals;
  fs->Locals[fs->LocalsSize].StartPC = fs->Proto->CodeSize;
  fs->LocalsSize++;
  fs->NumLocals++;
  if (fs->NumLocals > fs->MaxLocals) {
    fs->MaxLocals = fs->NumLocals;
  }
}

static void MarkCapturedLocal(MLuaFuncState *fs, int slot) {
  if (!fs || slot < 0 || slot >= 256) {
    return;
  }
  fs->CapturedSlots[(Size)slot / 32] |= (U32)1 << ((Size)slot % 32);
  if (slot > fs->MaxCapturedSlot) {
    fs->MaxCapturedSlot = slot;
  }
}

static Bool IsCapturedLocal(MLuaFuncState *fs, int slot) {
  if (!fs || slot < 0 || slot >= 256) {
    return FALSE;
  }
  return (fs->CapturedSlots[(Size)slot / 32] & ((U32)1 << ((Size)slot % 32))) !=
         0;
}

static Bool GrowScopeStack(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  Size newCap = (fs->ScopeCap == 0) ? 8 : fs->ScopeCap * 2;
  void *mem = MLuaAlloc(p->L, newCap * sizeof(fs->Scopes[0]));

  if (!mem) {
    Error(p, "out of memory");
    return FALSE;
  }
  if (fs->Scopes) {
    MemCpy(mem, fs->Scopes, fs->ScopeTop * sizeof(fs->Scopes[0]));
  }
  fs->Scopes = mem;
  fs->ScopeCap = newCap;
  return TRUE;
}

static void EmitLocalCleanup(MLuaParser *p, U8 base, U8 top) {
  MLuaFuncState *fs = p->FS;
  U8 slot;

  if (top <= base) {
    return;
  }
  if (fs->MaxCapturedSlot >= (int)base) {
    MLuaEmitOpB(fs, OP_CLOSE, base);
  }
  for (slot = base; slot < top; slot++) {
    MLuaEmitOpB(fs, OP_CLEARLOCAL, slot);
  }
}

static void BeginScope(MLuaParser *p, Bool breakBoundary) {
  MLuaFuncState *fs = p->FS;

  if (fs->ScopeTop >= fs->ScopeCap && !GrowScopeStack(p)) {
    return;
  }
  fs->Scopes[fs->ScopeTop].NumLocals = fs->NumLocals;
  fs->Scopes[fs->ScopeTop].LocalsSize = fs->LocalsSize;
  fs->Scopes[fs->ScopeTop].BreakBoundary = breakBoundary ? 1 : 0;
  fs->ScopeTop++;
}

static void EndScope(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  U8 base;
  U8 top;

  if (fs->ScopeTop == 0) {
    return;
  }
  base = fs->Scopes[fs->ScopeTop - 1].NumLocals;
  top = fs->NumLocals;
  EmitLocalCleanup(p, base, top);
  fs->NumLocals = base;
  fs->LocalsSize = fs->Scopes[fs->ScopeTop - 1].LocalsSize;
  fs->ScopeTop--;
}

static void EmitBreakCleanup(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  Size i;

  if (fs->ScopeTop == 0) {
    return;
  }
  for (i = fs->ScopeTop; i > 0; i--) {
    U8 base = fs->Scopes[i - 1].NumLocals;
    U8 top = (i == fs->ScopeTop) ? fs->NumLocals : fs->Scopes[i].NumLocals;
    EmitLocalCleanup(p, base, top);
    if (fs->Scopes[i - 1].BreakBoundary) {
      return;
    }
  }
}

/* ========================================================================== */
/* Upvalue Resolution                                                         */
/* ========================================================================== */

/* Variable kinds returned by ResolveVar */
enum { VAR_GLOBAL = 0, VAR_LOCAL = 1, VAR_UPVAL = 2 };

/* Compare an interned-name value against a raw (name, len) pair */
static Bool NameEquals(MLuaValue v, const char *name, Size len) {
  const char *s;
  Size slen;

  if (IsShortStr(v)) {
    slen = MLuaShortStrLen(v);
    s = MLuaStringData(v);
  } else if (IsString(v)) {
    s = MLuaStringData(v);
    slen = MLuaStringLen(v);
  } else {
    return FALSE;
  }

  return slen == len && s && MemCmp(s, name, len) == 0;
}

static int FindUpval(MLuaFuncState *fs, const char *name, Size len) {
  Size i;

  for (i = 0; i < fs->UpvalsSize; i++) {
    if (NameEquals(fs->Upvals[i].Name, name, len)) {
      return (int)i;
    }
  }
  return -1;
}

static int AddUpval(MLuaParser *p, MLuaFuncState *fs, const char *name,
                    Size len, U8 inStack, U8 index) {
  /* Upvalue indices are u8 operands; 255 is the hard cap (Lua's MAXUPVAL). */
  if (fs->UpvalsSize >= 255) {
    Error(p, "too many upvalues (max 255)");
    return -1;
  }

  if (fs->UpvalsSize >= fs->UpvalsCap) {
    Size newCap = (fs->UpvalsCap == 0) ? 4 : fs->UpvalsCap * 2;
    Size elemSize = sizeof(fs->Upvals[0]);
    void *mem = MLuaAlloc(p->L, newCap * elemSize);
    if (!mem) {
      Error(p, "out of memory");
      return -1;
    }
    if (fs->Upvals) {
      MemCpy(mem, fs->Upvals, fs->UpvalsSize * elemSize);
    }
    fs->Upvals = mem;
    fs->UpvalsCap = newCap;
  }

  fs->Upvals[fs->UpvalsSize].Name = MLuaStringNew(p->L, name, len);
  fs->Upvals[fs->UpvalsSize].InStack = inStack;
  fs->Upvals[fs->UpvalsSize].Index = index;
  return (int)fs->UpvalsSize++;
}

/*
 * Resolve a name through the lexical scope chain (Lua's singlevaraux).
 * - Local in the current function        -> VAR_LOCAL, *outIdx = slot
 * - Local/upvalue of an enclosing one    -> VAR_UPVAL, *outIdx = upvalue index
 *   (materializing pass-through upvalues in every intermediate function)
 * - Otherwise                            -> VAR_GLOBAL
 */
static int ResolveVar(MLuaParser *p, MLuaFuncState *fs, const char *name,
                      Size len, int *outIdx) {
  int slot, uv, kind, outer;

  if (fs == NULL) {
    return VAR_GLOBAL;
  }

  slot = FindLocal(fs, name, len);
  if (slot >= 0) {
    *outIdx = slot;
    return VAR_LOCAL;
  }

  uv = FindUpval(fs, name, len);
  if (uv >= 0) {
    *outIdx = uv;
    return VAR_UPVAL;
  }

  kind = ResolveVar(p, fs->Prev, name, len, &outer);
  if (kind == VAR_LOCAL) {
    /* fs->Prev's local 'outer' is now captured: remember the highest captured
     * slot so loops in that function know whether they must emit OP_CLOSE. */
    MarkCapturedLocal(fs->Prev, outer);
    *outIdx = AddUpval(p, fs, name, len, 1, (U8)outer);
    return (*outIdx >= 0) ? VAR_UPVAL : VAR_GLOBAL;
  }
  if (kind == VAR_UPVAL) {
    *outIdx = AddUpval(p, fs, name, len, 0, (U8)outer);
    return (*outIdx >= 0) ? VAR_UPVAL : VAR_GLOBAL;
  }

  return VAR_GLOBAL;
}

/* ========================================================================== */
/* Forward/Backward Jump Emission                                             */
/* ========================================================================== */

/*
 * Jumps come in two encodings, both within the 1-2 byte ISA:
 *
 * Short (default): 2-byte OP_JMP/JMPF/JMPT with a patched I8 offset. If a
 * patch target turns out to exceed +/-127, MLuaPatchJump flags JumpOverflow
 * and MLuaParse re-parses the whole chunk with p->LongJumps set.
 *
 * Long (re-parse mode): the offset is pushed via a placeholder constant
 * (OP_LOADK, 2 bytes) and consumed by 1-byte OP_JMP_S / OP_LOOP_S, so any
 * distance is representable. A conditional long jump inverts the condition
 * to hop over the 3-byte long-jump sequence:
 *     JMPF target   =>   JMPT +3 ; LOADK k ; JMP_S
 *
 * Backward jumps know their distance at emit time, so they pick the right
 * form immediately and never trigger a re-parse.
 */

static MLuaFwdJump EmitFwdJump(MLuaParser *p, MLuaOpCode op) {
  MLuaFuncState *fs = p->FS;
  MLuaFwdJump j;

  if (!p->LongJumps) {
    j.Pos = MLuaEmitOpB(fs, op, 0);
    j.K = -1;
    return j;
  }

  if (op == OP_JMPF) {
    MLuaEmitOpB(fs, OP_JMPT, 3);
  } else if (op == OP_JMPT) {
    MLuaEmitOpB(fs, OP_JMPF, 3);
  }
  j.K = MLuaAddConstantRaw(fs, MakeInt(0));
  if (j.K < 0 || j.K > 255) {
    Error(p, "too many constants in function");
    j.K = -1;
    j.Pos = 0;
    return j;
  }
  MLuaEmitOpB(fs, OP_LOADK, (U8)j.K);
  MLuaEmitOp(fs, OP_JMP_S);
  j.Pos = MLuaCodePos(fs); /* Offset origin: right after JMP_S */
  return j;
}

static void PatchFwdJump(MLuaParser *p, MLuaFwdJump j, Size target) {
  MLuaFuncState *fs = p->FS;

  if (j.K < 0) {
    MLuaPatchJump(fs, j.Pos, target);
  } else {
    fs->Proto->Constants[j.K] = MakeInt((I32)target - (I32)j.Pos);
  }
}

static void EmitBackJump(MLuaParser *p, MLuaOpCode op, Size target) {
  MLuaFuncState *fs = p->FS;
  int shortOff = (int)target - ((int)MLuaCodePos(fs) + 2);

  if (shortOff >= -128) {
    MLuaEmitOpB(fs, op, (U8)(I8)shortOff);
    return;
  }

  /* Long backward jump */
  {
    int k;
    Size afterPos;

    if (op == OP_JMPF) {
      MLuaEmitOpB(fs, OP_JMPT, 3);
    } else if (op == OP_JMPT) {
      MLuaEmitOpB(fs, OP_JMPF, 3);
    }
    k = MLuaAddConstantRaw(fs, MakeInt(0));
    if (k < 0 || k > 255) {
      Error(p, "too many constants in function");
      return;
    }
    MLuaEmitOpB(fs, OP_LOADK, (U8)k);
    MLuaEmitOp(fs, OP_LOOP_S);
    afterPos = MLuaCodePos(fs);
    fs->Proto->Constants[k] = MakeInt((I32)(afterPos - target));
  }
}

/* ========================================================================== */
/* Break Patching & Upvalue Closing                                           */
/* ========================================================================== */

/*
 * Pending forward jumps (breaks, if-chain end jumps) are collected on the
 * FuncState's PatchStack. Each construct records the stack depth on entry
 * (its watermark) and, at its resolution point, patches and pops every
 * entry above that watermark. Supports multiple breaks and nesting.
 */
static void PushPatch(MLuaParser *p, MLuaFwdJump j, Bool isBreak) {
  MLuaFuncState *fs = p->FS;

  if (fs->PatchTop >= fs->PatchCap) {
    Size newCap = (fs->PatchCap == 0) ? 8 : fs->PatchCap * 2;
    void *mem = MLuaAlloc(p->L, newCap * sizeof(MLuaFwdJump));
    if (!mem) {
      Error(p, "out of memory");
      return;
    }
    if (fs->PatchStack) {
      MemCpy(mem, fs->PatchStack, fs->PatchTop * sizeof(MLuaFwdJump));
    }
    fs->PatchStack = mem;
    fs->PatchCap = newCap;
  }

  j.IsBreak = isBreak ? 1 : 0;
  fs->PatchStack[fs->PatchTop++] = j;
}

static void PatchAll(MLuaParser *p, Size watermark, Size target) {
  MLuaFuncState *fs = p->FS;

  while (fs->PatchTop > watermark) {
    fs->PatchTop--;
    PatchFwdJump(p, fs->PatchStack[fs->PatchTop], target);
  }
}

/*
 * Patch and remove the if-chain end jumps above the watermark, keeping any
 * pending breaks (compacted in order) for the enclosing loop to resolve.
 * An if must not use PatchAll: a break inside one of its arms would be
 * captured and retargeted to the end of the if instead of the loop exit.
 */
static void PatchNonBreaks(MLuaParser *p, Size watermark, Size target) {
  MLuaFuncState *fs = p->FS;
  Size src, dst = watermark;

  for (src = watermark; src < fs->PatchTop; src++) {
    if (fs->PatchStack[src].IsBreak) {
      fs->PatchStack[dst++] = fs->PatchStack[src];
    } else {
      PatchFwdJump(p, fs->PatchStack[src], target);
    }
  }
  fs->PatchTop = dst;
}

/*
 * Emit OP_CLOSE for loop-scoped locals starting at 'base', but only when some
 * nested function actually captured a slot >= base anywhere in this function.
 * (Conservative: also covers slot reuse across sibling loops.) When nothing
 * is captured the loop pays zero bytes and zero cycles.
 */
static void EmitCloseIfCaptured(MLuaParser *p, U8 base) {
  MLuaFuncState *fs = p->FS;

  if (fs->MaxCapturedSlot >= (int)base) {
    MLuaEmitOpB(fs, OP_CLOSE, base);
  }
}

static void MarkLoopRange(U8 *mask, Size size, Size from, Size to) {
  Size i;
  if (from > to || from >= size) {
    return;
  }
  if (to > size) {
    to = size;
  }
  for (i = from; i < to; i++) {
    mask[i] = 1;
  }
}

static Bool HasLaterLocalRead(MLuaProto *proto, Size start, U8 slot) {
  Size pc = start;

  while (pc < proto->CodeSize) {
    MLuaOpCode op = (MLuaOpCode)proto->Code[pc];
    Size size = MLuaOpSize(op);

    if (size == 0 || pc + size > proto->CodeSize) {
      return TRUE;
    }
    if ((op == OP_GETLOCAL || op == OP_GETLOCAL_CLEAR) &&
        proto->Code[pc + 1] == slot) {
      return TRUE;
    }
    /* Fused indexing reads two locals through its packed nibbles */
    if ((op == OP_GETTABLE_LL || op == OP_SETTABLE_LL) &&
        ((proto->Code[pc + 1] >> 4) == slot ||
         (proto->Code[pc + 1] & 0x0F) == slot)) {
      return TRUE;
    }
    pc += size;
  }
  return FALSE;
}

static void OptimizeLastLocalUses(MLuaFuncState *fs) {
  MLuaProto *proto = fs->Proto;
  U8 *loopMask;
  Size pc;
  Bool hasLongBackward = FALSE;

  if (!proto || proto->CodeSize == 0) {
    return;
  }

  loopMask = (U8 *)MLuaAlloc(fs->L, proto->CodeSize);
  if (!loopMask) {
    return;
  }
  MemSet(loopMask, 0, proto->CodeSize);

  pc = 0;
  while (pc < proto->CodeSize) {
    MLuaOpCode op = (MLuaOpCode)proto->Code[pc];
    Size size = MLuaOpSize(op);

    if (size == 0 || pc + size > proto->CodeSize) {
      return;
    }
    if ((op == OP_JMP || op == OP_JMPF || op == OP_JMPT) && size == 2) {
      I8 off = (I8)proto->Code[pc + 1];
      if (off < 0) {
        Size target = (Size)((IPtr)(pc + 2) + (IPtr)off);
        MarkLoopRange(loopMask, proto->CodeSize, target, pc + size);
      }
    } else if (op == OP_LOOP && size == 2) {
      U8 off = proto->Code[pc + 1];
      Size target = (off > pc) ? 0 : pc - off;
      MarkLoopRange(loopMask, proto->CodeSize, target, pc + size);
    } else if (op == OP_LOOP_S) {
      hasLongBackward = TRUE;
    } else if (op == OP_NLOOP_PREP && size == 2) {
      U8 base = proto->Code[pc + 1];
      Size scan = pc + size;
      while (scan < proto->CodeSize) {
        MLuaOpCode scanOp = (MLuaOpCode)proto->Code[scan];
        Size scanSize = MLuaOpSize(scanOp);
        if (scanSize == 0 || scan + scanSize > proto->CodeSize) {
          return;
        }
        if (scanOp == OP_NLOOP_STEP && scanSize == 2 &&
            proto->Code[scan + 1] == base) {
          MarkLoopRange(loopMask, proto->CodeSize, pc, scan + scanSize);
          break;
        }
        scan += scanSize;
      }
    }
    pc += size;
  }

  if (hasLongBackward) {
    return;
  }

  pc = 0;
  while (pc < proto->CodeSize) {
    MLuaOpCode op = (MLuaOpCode)proto->Code[pc];
    Size size = MLuaOpSize(op);

    if (size == 0 || pc + size > proto->CodeSize) {
      return;
    }
    if (op == OP_GETLOCAL && !loopMask[pc]) {
      U8 slot = proto->Code[pc + 1];
      if (!IsCapturedLocal(fs, slot) &&
          !HasLaterLocalRead(proto, pc + size, slot)) {
        proto->Code[pc] = (U8)OP_GETLOCAL_CLEAR;
      }
    }
    pc += size;
  }
}

/* ========================================================================== */
/* Multi-Result Handling                                                      */
/* ========================================================================== */

/*
 * Calls (and `...`) can produce any number of values at runtime. The parser
 * marks where the last such instruction ended; a context that needs an
 * exact count emits OP_ADJUST right after it, while MULTRET contexts
 * (final call argument, final return expression, final constructor element)
 * use the CALLM/APPENDM forms or the natural return flow instead.
 */

/* Emit a call-family instruction and remember that the expression ends
 * with a multi-result producer */
static void EmitCallOp(MLuaParser *p, MLuaOpCode op, U8 operand) {
  MLuaFuncState *fs = p->FS;
  MLuaEmitOpB(fs, op, operand);
  fs->LastCallEnd = MLuaCodePos(fs);
}

/* Does the code emitted so far end exactly with a multi-result producer? */
static Bool EndsWithMultret(MLuaFuncState *fs) {
  return fs->LastCallEnd != 0 && fs->LastCallEnd == MLuaCodePos(fs);
}

/* In a single-value context: pin a trailing call to exactly one result */
static void AdjustTo1(MLuaParser *p) {
  if (EndsWithMultret(p->FS)) {
    MLuaEmitOpB(p->FS, OP_ADJUST, 1);
  }
}

/*
 * Emit the read of a named variable using full lexical resolution.
 */
static void EmitGetVar(MLuaParser *p, const char *name, Size len) {
  MLuaFuncState *fs = p->FS;
  int idx;

  switch (ResolveVar(p, fs, name, len, &idx)) {
  case VAR_LOCAL:
    MLuaEmitOpB(fs, OP_GETLOCAL, (U8)idx);
    break;
  case VAR_UPVAL:
    MLuaEmitOpB(fs, OP_GETUPVAL, (U8)idx);
    break;
  default: {
    int k = MLuaAddStringK(fs, name, len);
    MLuaEmitOpB(fs, OP_GETGLOBAL_K, (U8)k);
    break;
  }
  }
}

/* ========================================================================== */
/* Expression Parsing (Pratt Parser)                                          */
/* ========================================================================== */

typedef enum {
  PREC_NONE,
  PREC_OR,      /* or */
  PREC_AND,     /* and */
  PREC_COMPARE, /* < > <= >= ~= == */
  PREC_CONCAT,  /* .. */
  PREC_ADD,     /* + - */
  PREC_MUL,     /* * / % */
  PREC_UNARY,   /* not # - */
  PREC_POW      /* ^ */
} Precedence;

static MLuaOpCode BinaryOp(MLuaTokenType t) {
  switch (t) {
  case TK_PLUS:
    return OP_ADD;
  case TK_MINUS:
    return OP_SUB;
  case TK_STAR:
    return OP_MUL;
  case TK_SLASH:
    return OP_DIV;
  case TK_PERCENT:
    return OP_MOD;
  case TK_CARET:
    return OP_POW;
  case TK_EQ:
    return OP_EQ;
  case TK_NE:
    return OP_NEQ;
  case TK_LT:
    return OP_LT;
  case TK_GT:
    return OP_LT; /* Swap operands */
  case TK_LE:
    return OP_LE;
  case TK_GE:
    return OP_LE; /* Swap operands */
  default:
    return OP_NOP;
  }
}

static Precedence GetPrecedence(MLuaTokenType t) {
  switch (t) {
  case TK_OR:
    return PREC_OR;
  case TK_AND:
    return PREC_AND;
  case TK_EQ:
  case TK_NE:
  case TK_LT:
  case TK_GT:
  case TK_LE:
  case TK_GE:
    return PREC_COMPARE;
  case TK_CONCAT:
    return PREC_CONCAT;
  case TK_PLUS:
  case TK_MINUS:
    return PREC_ADD;
  case TK_STAR:
  case TK_SLASH:
  case TK_PERCENT:
    return PREC_MUL;
  case TK_CARET:
    return PREC_POW;
  default:
    return PREC_NONE;
  }
}

static Bool IsRightAssoc(MLuaTokenType t) {
  return t == TK_CARET || t == TK_CONCAT;
}

static void ParseSubExpr(MLuaParser *p, Precedence minPrec);

static void ParsePrefix(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;

  switch (p->Lex.Token.Type) {
  case TK_NIL:
    Advance(p);
    MLuaEmitOp(fs, OP_LOADNIL);
    StackPush(p, 1);
    break;

  case TK_TRUE:
    Advance(p);
    MLuaEmitOp(fs, OP_LOADTRUE);
    StackPush(p, 1);
    break;

  case TK_FALSE:
    Advance(p);
    MLuaEmitOp(fs, OP_LOADFALSE);
    StackPush(p, 1);
    break;

  case TK_NUMBER: {
    double num = p->Lex.Token.Value.Number;
    Bool isFloat = p->Lex.Token.NumberIsFloat;
    Advance(p);

    if (!isFloat && num == (double)(I32)num) {
      /* Integer literal */
      if ((I32)num >= -128 && (I32)num <= 127) {
        MLuaEmitOpB(fs, OP_LOADINT, (U8)(I8)(I32)num);
      } else {
        int k = MLuaAddConstant(fs, MLuaMakeInt(p->L, (I32)num));
        MLuaEmitOpB(fs, OP_LOADK, (U8)k);
      }
    } else {
      /* Float literal (or integer too large for I32): float constant */
      int k = MLuaAddConstant(fs, isFloat ? MLuaMakeFloat(p->L, num)
                                          : MLuaMakeNumber(p->L, num));
      MLuaEmitOpB(fs, OP_LOADK, (U8)k);
    }
    StackPush(p, 1);
    break;
  }

  case TK_STRING: {
    int k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                           p->Lex.Token.Value.String.Length);
    Advance(p);
    MLuaEmitOpB(fs, OP_LOADK, (U8)k);
    StackPush(p, 1);
    break;
  }

  case TK_NAME: {
    const char *name = p->Lex.Token.Value.String.Data;
    Size len = p->Lex.Token.Value.String.Length;
    Advance(p);

    EmitGetVar(p, name, len);
    StackPush(p, 1);
    break;
  }

  case TK_LPAREN:
    Advance(p);
    ParseExpr(p);
    Expect(p, TK_RPAREN);
    /* Lua: a parenthesized expression is exactly one value */
    AdjustTo1(p);
    break;

  case TK_LBRACE:
    /* Table constructor */
    {
      int count = 0;
      Advance(p);
      MLuaEmitOp(fs, OP_NEWTABLE);
      StackPush(p, 1);

      while (!Check(p, TK_RBRACE) && !Check(p, TK_EOF)) {
        if (Check(p, TK_LBRACKET)) {
          /* [key] = value */
          Advance(p);
          ParseExpr(p); /* key */
          AdjustTo1(p);
          Expect(p, TK_RBRACKET);
          Expect(p, TK_ASSIGN);
          ParseExpr(p); /* value */
          AdjustTo1(p);
          MLuaEmitOp(fs, OP_SETTABLE);
          StackPop(p, 2);
        } else if (Check(p, TK_NAME) && MLuaLexPeek(&p->Lex) == TK_ASSIGN) {
          /* name = value */
          int k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                                 p->Lex.Token.Value.String.Length);
          Advance(p); /* name */
          Advance(p); /* = */
          ParseExpr(p);
          AdjustTo1(p);
          MLuaEmitOpB(fs, OP_LOADK, (U8)(U16)k);
          MLuaEmitOp(fs, OP_SWAP);
          MLuaEmitOp(fs, OP_SETTABLE);
          StackPop(p, 1);
        } else {
          /* Array element. The FINAL element expands all its call/vararg
           * results (Lua semantics); non-final ones contribute one value. */
          Bool multret;
          Bool sep;

          ParseExpr(p);
          multret = EndsWithMultret(fs);
          sep = Match(p, TK_COMMA) || Match(p, TK_SEMICOLON);

          if (multret && (!sep || Check(p, TK_RBRACE))) {
            MLuaEmitOp(fs, OP_APPENDM);
          } else {
            if (multret) {
              MLuaEmitOpB(fs, OP_ADJUST, 1);
            }
            MLuaEmitOp(fs, OP_APPEND);
          }
          StackPop(p, 1);
          count++;

          if (!sep) {
            break;
          }
          continue; /* Separator already consumed */
        }

        if (!Match(p, TK_COMMA) && !Match(p, TK_SEMICOLON)) {
          break;
        }
      }
      Expect(p, TK_RBRACE);
      UNUSED(count);
    }
    break;

  case TK_MINUS:
    Advance(p);
    ParseSubExpr(p, PREC_UNARY);
    AdjustTo1(p);
    MLuaEmitOp(fs, OP_UNM);
    break;

  case TK_NOT:
    Advance(p);
    ParseSubExpr(p, PREC_UNARY);
    AdjustTo1(p);
    MLuaEmitOp(fs, OP_NOT);
    break;

  case TK_HASH:
    Advance(p);
    ParseSubExpr(p, PREC_UNARY);
    AdjustTo1(p);
    MLuaEmitOp(fs, OP_LEN);
    break;

  case TK_FUNCTION: {
    /* Anonymous function expression: function(...) ... end */
    int protoIdx;
    Advance(p); /* consume 'function' */
    protoIdx = ParseFuncBody(p, 0);
    MLuaEmitOpB(fs, OP_CLOSURE, (U8)protoIdx);
    StackPush(p, 1);
    break;
  }

  case TK_DOTS: {
    /* Varargs expression: ... */
    /* Only valid in vararg functions */
    if (!fs->Proto->IsVararg) {
      Error(p, "'...' used outside vararg function");
    }
    Advance(p);
    /* Push ALL varargs; treated like a call's results so the surrounding
     * context truncates (ADJUST 1) or expands (CALLM/APPENDM/return) it. */
    EmitCallOp(p, OP_VARARG, 0);
    StackPush(p, 1);
    break;
  }

  default:
    Error(p, "unexpected token in expression");
    /* CONSUME the offending token so the parser always makes progress
     * (otherwise an unexpected token loops the statement parser forever),
     * and push nil as a placeholder value. */
    Advance(p);
    MLuaEmitOp(fs, OP_LOADNIL);
    StackPush(p, 1);
    break;
  }
}

/*
 * AccessInfo tracks the last table/field access for possible assignment.
 * If accessType != 0, we have pending access info on the stack.
 */
typedef struct {
  int accessType;    /* 0=none, 1=table[key], 2=table.field */
  U16 fieldConstIdx; /* For field access, the constant index */
} AccessInfo;

/*
 * If the two most recently emitted instructions are exactly
 * `OP_GETLOCAL x; OP_GETLOCAL y` with both slots <= 15, retract them and
 * return TRUE with the nibble-packed operand in *slots. Used to fuse a
 * pending t[k] access into one OP_GETTABLE_LL / OP_SETTABLE_LL.
 *
 * Retraction safety: only the last two instructions are removed, so every
 * position recorded earlier (patch-stack jump positions, loop starts, line
 * map entries, LastCallEnd) is at or before the retraction point. Forward
 * jumps are patched with targets computed from live positions at patch
 * time, and no patch can occur between the two GETLOCALs (a bare local
 * read emits nothing else; and/or always place a jump or pop between
 * loads), so no already-patched target can point inside the pair. A line
 * map entry at the first GETLOCAL still lands on the boundary of whatever
 * is emitted next.
 */
static Bool TryRetractLocalPair(MLuaFuncState *fs, U8 *slots) {
  MLuaProto *proto = fs->Proto;
  Size last = fs->LastInstrPos;
  Size prev = fs->PrevInstrPos;
  U8 t;
  U8 k;

  if (last == (Size)-1 || prev == (Size)-1) {
    return FALSE;
  }
  /* The pair must be adjacent and must end the code buffer */
  if (last != prev + 2 || proto->CodeSize != last + 2) {
    return FALSE;
  }
  if (proto->Code[prev] != OP_GETLOCAL || proto->Code[last] != OP_GETLOCAL) {
    return FALSE;
  }
  t = proto->Code[prev + 1];
  k = proto->Code[last + 1];
  if (t > 15 || k > 15) {
    return FALSE;
  }

  proto->CodeSize = prev;
  fs->LastInstrPos = (Size)-1;
  fs->PrevInstrPos = (Size)-1;
  *slots = (U8)((t << 4) | k);
  return TRUE;
}

/*
 * Reinsert a retracted `GETLOCAL t; GETLOCAL k` pair at `pos` (the start of
 * the code emitted since the retraction). Fusion-repair path: safe because
 * the block after `pos` is a single value expression, which cannot contain
 * pending patches, break jumps, or line-map entries.
 */
static void InsertLocalPairAt(MLuaFuncState *fs, Size pos, U8 slots) {
  U8 bytes[4];
  bytes[0] = (U8)OP_GETLOCAL;
  bytes[1] = (U8)(slots >> 4);
  bytes[2] = (U8)OP_GETLOCAL;
  bytes[3] = (U8)(slots & 0x0F);
  MLuaInsertBytes(fs, pos, bytes, 4);
}

/*
 * Complete a pending suffix access by emitting its table read (shared by
 * every "complete pending access first" site). A t[k] access whose table
 * and key were two just-emitted local loads becomes one OP_GETTABLE_LL.
 */
static void EmitPendingGet(MLuaParser *p, AccessInfo *info) {
  MLuaFuncState *fs = p->FS;

  if (info->accessType == 1) {
    U8 slots;
    if (TryRetractLocalPair(fs, &slots)) {
      MLuaEmitOpB(fs, OP_GETTABLE_LL, slots);
    } else {
      MLuaEmitOp(fs, OP_GETTABLE);
    }
    StackPop(p, 1);
  } else if (info->accessType == 2) {
    MLuaEmitOpB(fs, OP_LOADK, (U8)info->fieldConstIdx);
    MLuaEmitOp(fs, OP_GETTABLE);
  }
  info->accessType = 0;
}

static AccessInfo ParseSuffixForAssign(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  AccessInfo info = {0, 0};

  for (;;) {
    switch (p->Lex.Token.Type) {
    case TK_DOT: {
      /* .field - complete any pending access first */
      EmitPendingGet(p, &info);

      Advance(p);
      if (!Check(p, TK_NAME)) {
        Error(p, "expected field name");
        info.accessType = 0;
        return info;
      }
      info.fieldConstIdx = (U16)MLuaAddStringK(
          fs, p->Lex.Token.Value.String.Data, p->Lex.Token.Value.String.Length);
      Advance(p);
      info.accessType = 2; /* field access */
      break;
    }

    case TK_LBRACKET: {
      /* [key] - complete any pending access first */
      EmitPendingGet(p, &info);

      Advance(p);
      ParseExpr(p);
      AdjustTo1(p);
      Expect(p, TK_RBRACKET);
      info.accessType = 1; /* table[key] access */
      info.fieldConstIdx = 0;
      break;
    }

    case TK_LPAREN: {
      /* Function call - complete pending access */
      EmitPendingGet(p, &info);

      int argCount = 0;
      Advance(p);
      while (!Check(p, TK_RPAREN) && !Check(p, TK_EOF)) {
        ParseExpr(p);
        argCount++;
        if (!Match(p, TK_COMMA))
          break;
        /* Non-final argument: exactly one value */
        AdjustTo1(p);
      }
      Expect(p, TK_RPAREN);
      if (argCount > 0 && EndsWithMultret(fs)) {
        /* Final argument expands: pass all of its results */
        EmitCallOp(p, OP_CALLM, (U8)(argCount - 1));
      } else {
        EmitCallOp(p, OP_CALL, (U8)argCount);
      }
      StackPop(p, argCount);
      break;
    }

    case TK_STRING: {
      /* f"string" - complete pending access */
      EmitPendingGet(p, &info);

      int k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                             p->Lex.Token.Value.String.Length);
      Advance(p);
      MLuaEmitOpB(fs, OP_LOADK, (U8)k);
      StackPush(p, 1);
      EmitCallOp(p, OP_CALL, 1);
      StackPop(p, 1);
      break;
    }

    case TK_LBRACE: {
      /* f{table} - complete pending access */
      EmitPendingGet(p, &info);

      ParsePrefix(p);
      EmitCallOp(p, OP_CALL, 1);
      StackPop(p, 1);
      break;
    }

    case TK_COLON: {
      /* Method call: t:name(args) — sugar for t.name(t, args) */
      int methodK;
      int argCount = 1; /* implicit self */

      /* Complete pending access so the receiver is at TOS */
      EmitPendingGet(p, &info);

      Advance(p);
      if (!Check(p, TK_NAME)) {
        Error(p, "expected method name after ':'");
        return info;
      }
      methodK = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                               p->Lex.Token.Value.String.Length);
      Advance(p);

      /* [t] -> [func, t]: duplicate receiver, look up method, reorder */
      MLuaEmitOp(fs, OP_DUP);
      StackPush(p, 1);
      MLuaEmitOpB(fs, OP_LOADK, (U8)methodK);
      StackPush(p, 1);
      MLuaEmitOp(fs, OP_GETTABLE);
      StackPop(p, 1);
      MLuaEmitOp(fs, OP_SWAP);

      /* Arguments: (expr list), "string" or {table} */
      if (Check(p, TK_LPAREN)) {
        Advance(p);
        while (!Check(p, TK_RPAREN) && !Check(p, TK_EOF)) {
          ParseExpr(p);
          argCount++;
          if (!Match(p, TK_COMMA))
            break;
          /* Non-final argument: exactly one value */
          AdjustTo1(p);
        }
        Expect(p, TK_RPAREN);
      } else if (Check(p, TK_STRING)) {
        int k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                               p->Lex.Token.Value.String.Length);
        Advance(p);
        MLuaEmitOpB(fs, OP_LOADK, (U8)k);
        StackPush(p, 1);
        argCount++;
      } else if (Check(p, TK_LBRACE)) {
        ParsePrefix(p);
        argCount++;
      } else {
        Error(p, "expected arguments after method name");
        return info;
      }

      if (argCount > 1 && EndsWithMultret(fs)) {
        EmitCallOp(p, OP_CALLM, (U8)(argCount - 1));
      } else {
        EmitCallOp(p, OP_CALL, (U8)argCount);
      }
      StackPop(p, argCount);
      break;
    }

    default:
      return info;
    }
  }
}

static void ParseSuffix(MLuaParser *p) {
  AccessInfo info = ParseSuffixForAssign(p);

  /* Complete any pending access - we're not assigning */
  EmitPendingGet(p, &info);
}

static void ParseSubExpr(MLuaParser *p, Precedence minPrec) {
  MLuaFuncState *fs = p->FS;

  if (!EnterParseDepth(p)) {
    return;
  }

  ParsePrefix(p);
  ParseSuffix(p);

  while (!p->Error && (GetPrecedence(p->Lex.Token.Type) > minPrec ||
                       (GetPrecedence(p->Lex.Token.Type) == minPrec &&
                        IsRightAssoc(p->Lex.Token.Type)))) {
    MLuaTokenType op = p->Lex.Token.Type;
    Precedence prec = GetPrecedence(op);
    /* Recurse at the operator's own precedence for BOTH associativities.
     * The loop guard above (strict '>' for left-assoc, plus the
     * '== minPrec && right-assoc' clause) is what distinguishes them:
     *   left-assoc  a-b-c : the inner parse stops at the second '-' (ADD is
     *                       not > ADD), so the outer loop folds left.
     *   right-assoc a^b^c : the inner parse consumes the second '^' via the
     *                       equality clause, folding right.
     * Using (prec + 1) here for left-assoc was the bug: it made the RHS of a
     * lower-precedence op refuse a following higher-precedence op, so
     * `2 + 3 * 4` parsed as `(2 + 3) * 4`. */
    Precedence nextPrec = prec;

    /* The lhs operand of any operator is a single value */
    AdjustTo1(p);

    /* Handle short-circuit operators specially.
     * The condition value IS the result when the jump is taken, but
     * JMPF/JMPT pop their operand — so test a DUP of it. */
    if (op == TK_AND) {
      MLuaFwdJump jmp;
      Advance(p);
      MLuaEmitOp(fs, OP_DUP);
      StackPush(p, 1);
      jmp = EmitFwdJump(p, OP_JMPF); /* Jump if false (pops the dup) */
      StackPop(p, 1);
      MLuaEmitOpB(fs, OP_POP, 1); /* Discard lhs, rhs replaces it */
      StackPop(p, 1);
      ParseSubExpr(p, nextPrec);
      AdjustTo1(p);
      PatchFwdJump(p, jmp, MLuaCodePos(fs));
    } else if (op == TK_OR) {
      MLuaFwdJump jmp;
      Advance(p);
      MLuaEmitOp(fs, OP_DUP);
      StackPush(p, 1);
      jmp = EmitFwdJump(p, OP_JMPT); /* Jump if true (pops the dup) */
      StackPop(p, 1);
      MLuaEmitOpB(fs, OP_POP, 1); /* Discard lhs, rhs replaces it */
      StackPop(p, 1);
      ParseSubExpr(p, nextPrec);
      AdjustTo1(p);
      PatchFwdJump(p, jmp, MLuaCodePos(fs));
    } else if (op == TK_CONCAT) {
      /* Collect all concatenations */
      int count = 1;
      Advance(p);
      ParseSubExpr(p, nextPrec);
      AdjustTo1(p);
      count++;

      while (Check(p, TK_CONCAT)) {
        Advance(p);
        ParseSubExpr(p, nextPrec);
        AdjustTo1(p);
        count++;
      }

      MLuaEmitOpB(fs, OP_CONCAT, (U8)count);
      StackPop(p, count - 1);
    } else {
      /* Regular binary op */
      Bool swap = (op == TK_GT || op == TK_GE);
      MLuaOpCode binOp = BinaryOp(op);

      Advance(p);

      ParseSubExpr(p, nextPrec);
      AdjustTo1(p);

      /* a > b  ==  b < a: with [a, b] on the stack, SWAP yields [b, a]
       * and OP_LT then computes b < a. Same for >= via OP_LE. */
      if (swap) {
        MLuaEmitOp(fs, OP_SWAP);
      }

      MLuaEmitOp(fs, binOp);
      StackPop(p, 1); /* Two operands -> one result */
    }
  }

  LeaveParseDepth(p);
}

static void ParseExpr(MLuaParser *p) { ParseSubExpr(p, PREC_NONE); }

/* ========================================================================== */
/* Statements                                                                 */
/* ========================================================================== */

static void ParseLocal(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  int varCount = 0;
  int exprCount = 0;

  Advance(p); /* Skip 'local' */

  if (Check(p, TK_FUNCTION)) {
    /* local function name ... */
    int protoIdx;
    Advance(p);
    if (!Check(p, TK_NAME)) {
      Error(p, "expected function name");
      return;
    }
    AddLocal(p, p->Lex.Token.Value.String.Data,
             p->Lex.Token.Value.String.Length);
    Advance(p);
    /* Parse function body and create closure */
    protoIdx = ParseFuncBody(p, FALSE);
    if (protoIdx >= 0) {
      MLuaEmitOpB(fs, OP_CLOSURE, (U8)protoIdx);
    } else {
      MLuaEmitOp(fs, OP_LOADNIL);
    }
    MLuaEmitOpB(fs, OP_SETLOCAL, fs->NumLocals - 1);
    return;
  }

  /* local name [, name]* */
  do {
    if (!Check(p, TK_NAME)) {
      Error(p, "expected variable name");
      return;
    }
    AddLocal(p, p->Lex.Token.Value.String.Data,
             p->Lex.Token.Value.String.Length);
    Advance(p);
    varCount++;
  } while (Match(p, TK_COMMA));

  /* = expr [, expr]* */
  if (Match(p, TK_ASSIGN)) {
    for (;;) {
      ParseExpr(p);
      exprCount++;
      if (!Match(p, TK_COMMA)) {
        break;
      }
      /* Non-final expression: exactly one value */
      AdjustTo1(p);
    }
  }

  /* Normalize the value list to exactly varCount values */
  {
    int i;
    Bool multret = (exprCount > 0) && EndsWithMultret(fs);

    if (exprCount <= varCount) {
      int missing = varCount - exprCount;
      if (multret) {
        /* The final call provides its 1 counted slot plus the missing
         * values: pin it to exactly that many */
        MLuaEmitOpB(fs, OP_ADJUST, (U8)(missing + 1));
        StackPush(p, missing);
      } else {
        for (i = 0; i < missing; i++) {
          MLuaEmitOp(fs, OP_LOADNIL);
          StackPush(p, 1);
        }
      }
      exprCount = varCount;
    } else {
      /* More expressions than variables: evaluate and discard extras */
      if (multret) {
        MLuaEmitOpB(fs, OP_ADJUST, 1);
      }
      MLuaEmitOpB(fs, OP_POP, (U8)(exprCount - varCount));
      StackPop(p, exprCount - varCount);
      exprCount = varCount;
    }

    /* Pop values into locals (reverse order) */
    for (i = varCount - 1; i >= 0; i--) {
      MLuaEmitOpB(fs, OP_SETLOCAL, fs->NumLocals - varCount + i);
      StackPop(p, 1);
    }
  }
}

static void ParseAssignment(MLuaParser *p, const char *name, Size len) {
  MLuaFuncState *fs = p->FS;
  int idx;

  Expect(p, TK_ASSIGN);
  ParseExpr(p);
  AdjustTo1(p);

  switch (ResolveVar(p, fs, name, len, &idx)) {
  case VAR_LOCAL:
    MLuaEmitOpB(fs, OP_SETLOCAL, (U8)idx);
    break;
  case VAR_UPVAL:
    MLuaEmitOpB(fs, OP_SETUPVAL, (U8)idx);
    break;
  default: {
    int k = MLuaAddStringK(fs, name, len);
    MLuaEmitOpB(fs, OP_LOADK, (U8)k);
    MLuaEmitOp(fs, OP_SWAP);
    MLuaEmitOp(fs, OP_SETGLOBAL);
    break;
  }
  }
  StackPop(p, 1);
}

static void ParseReturn(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  int retCount = 0;

  Advance(p); /* Skip 'return' */

  if (!Check(p, TK_END) && !Check(p, TK_ELSE) && !Check(p, TK_ELSEIF) &&
      !Check(p, TK_UNTIL) && !Check(p, TK_EOF) && !Check(p, TK_SEMICOLON)) {
    for (;;) {
      ParseExpr(p);
      retCount++;
      if (!Match(p, TK_COMMA)) {
        break;
      }
      /* Non-final return expression: exactly one value (the final one
       * naturally returns everything — OP_RET passes the whole stack) */
      AdjustTo1(p);
    }
  }

  /* `return f(...)`: turn the final OP_CALL into a tail call so the frame
   * is reused (constant frame space for tail recursion). Only the plain
   * OP_CALL form, in clean return position, is flipped. */
  if (retCount == 1 && EndsWithMultret(fs) && fs->LastCallEnd >= 2 &&
      fs->Proto->Code[fs->LastCallEnd - 2] == OP_CALL) {
    fs->Proto->Code[fs->LastCallEnd - 2] = OP_TAILCALL;
  }

  MLuaEmitOpB(fs, OP_RET, (U8)retCount);
  StackPop(p, retCount);
}

static void ParseIf(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  Size endWatermark = fs->PatchTop; /* End-jumps of every taken arm */
  MLuaFwdJump jmpToElse;
  Bool haveElse = FALSE;

  Advance(p); /* Skip 'if' */

  ParseExpr(p);
  AdjustTo1(p);
  Expect(p, TK_THEN);

  jmpToElse = EmitFwdJump(p, OP_JMPF);
  StackPop(p, 1);

  BeginScope(p, FALSE);
  ParseBlock(p);
  EndScope(p);

  while (Check(p, TK_ELSEIF)) {
    PushPatch(p, EmitFwdJump(p, OP_JMP), FALSE); /* Arm done -> jump to end */

    PatchFwdJump(p, jmpToElse, MLuaCodePos(fs));
    Advance(p); /* Skip 'elseif' */

    ParseExpr(p);
    Expect(p, TK_THEN);

    jmpToElse = EmitFwdJump(p, OP_JMPF);
    StackPop(p, 1);

    BeginScope(p, FALSE);
    ParseBlock(p);
    EndScope(p);
  }

  if (Check(p, TK_ELSE)) {
    PushPatch(p, EmitFwdJump(p, OP_JMP), FALSE); /* Arm done -> jump to end */

    PatchFwdJump(p, jmpToElse, MLuaCodePos(fs));
    haveElse = TRUE;

    Advance(p); /* Skip 'else' */
    BeginScope(p, FALSE);
    ParseBlock(p);
    EndScope(p);
  }

  Expect(p, TK_END);

  if (!haveElse) {
    PatchFwdJump(p, jmpToElse, MLuaCodePos(fs));
  }
  PatchNonBreaks(p, endWatermark, MLuaCodePos(fs));
}

static void ParseWhile(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  Size loopStart = MLuaCodePos(fs);
  Size breakWatermark = fs->PatchTop;
  U8 bodyBase;
  MLuaFwdJump jmpToEnd;
  Size exitPos;

  Advance(p); /* Skip 'while' */

  ParseExpr(p);
  AdjustTo1(p);
  Expect(p, TK_DO);

  jmpToEnd = EmitFwdJump(p, OP_JMPF);
  StackPop(p, 1);

  bodyBase = fs->NumLocals; /* Body locals start here */
  BeginScope(p, TRUE);
  ParseBlock(p);
  Expect(p, TK_END);

  /* End-of-iteration: close captures of body locals, then jump back */
  EndScope(p);
  EmitCloseIfCaptured(p, bodyBase);
  EmitBackJump(p, OP_JMP, loopStart);

  /* Exit point: condition-false and breaks land here */
  exitPos = MLuaCodePos(fs);
  EmitCloseIfCaptured(p, bodyBase);

  PatchFwdJump(p, jmpToEnd, exitPos);
  PatchAll(p, breakWatermark, exitPos);
}

static void ParseRepeat(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  Size loopStart = MLuaCodePos(fs);
  Size breakWatermark = fs->PatchTop;
  U8 bodyBase = fs->NumLocals;
  Size exitPos;

  Advance(p); /* Skip 'repeat' */

  BeginScope(p, TRUE);
  ParseBlock(p);
  Expect(p, TK_UNTIL);

  /* The until-condition can still see (and capture) body locals, so the
   * per-iteration close goes after the condition but before the jump. */
  ParseExpr(p);
  AdjustTo1(p);
  EndScope(p);
  EmitCloseIfCaptured(p, bodyBase);

  /* Jump back if condition is false */
  EmitBackJump(p, OP_JMPF, loopStart);
  StackPop(p, 1);

  exitPos = MLuaCodePos(fs);
  EmitCloseIfCaptured(p, bodyBase);
  PatchAll(p, breakWatermark, exitPos);
}

/* ========================================================================== */
/* For Loops                                                                  */
/* ========================================================================== */

static void ParseFor(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  Size breakWatermark = fs->PatchTop;
  int baseSlot;

  Advance(p); /* Skip 'for' */

  if (!Check(p, TK_NAME)) {
    Error(p, "expected variable name");
    return;
  }

  /* Save the loop variable name */
  const char *varName = p->Lex.Token.Value.String.Data;
  Size varLen = p->Lex.Token.Value.String.Length;
  Advance(p);

  if (Match(p, TK_ASSIGN)) {
    /* Numeric for: for i = start, limit [, step] do ... end
     *
     * Stored State pattern:
     *   Push: Start, Limit, Step, Exit_Offset, Body_Offset (5 values)
     *   FORPREP pops all 5, stores to 4 local slots, pushes Start if loop runs
     *   Loop body stores TOS to visible variable
     *   FORLOOP increments, reads Body_Offset from local, jumps if continuing
     *
     * Local slot layout (5 slots total):
     *   baseSlot+0: Index (current value)
     *   baseSlot+1: Limit
     *   baseSlot+2: Step
     *   baseSlot+3: Body_Offset (PC address for jump-back)
     *   baseSlot+4: Visible loop variable (user's 'i')
     */
    baseSlot = fs->NumLocals;
    AddLocal(p, "(for idx)", 9);  /* slot+0: idx */
    AddLocal(p, "(for lim)", 9);  /* slot+1: limit */
    AddLocal(p, "(for stp)", 9);  /* slot+2: step */
    AddLocal(p, "(for ptr)", 9);  /* slot+3: body pointer */
    AddLocal(p, varName, varLen); /* slot+4: visible loop variable */

    /* Push start onto stack */
    ParseExpr(p);
    AdjustTo1(p);

    Expect(p, TK_COMMA);

    /* Push limit onto stack */
    ParseExpr(p);
    AdjustTo1(p);

    /* Push step onto stack (default 1) */
    if (Match(p, TK_COMMA)) {
      ParseExpr(p);
      AdjustTo1(p);
    } else {
      MLuaEmitOpB(fs, OP_LOADINT, 1);
      StackPush(p, 1);
    }

    Expect(p, TK_DO);

    /* Loop targets are ABSOLUTE bytecode positions. They are pushed through
     * placeholder constants (LOADK is still a 2-byte instruction) instead of
     * LOADINT operands so that positions beyond 127 work; the constant values
     * are patched once the positions are known.
     *
     * Layout:
     *   [LOADK exitK]          2 bytes
     *   [LOADK bodyK]          2 bytes
     *   [NLOOP_PREP base]      2 bytes
     *   [SETLOCAL visible]     2 bytes  <- bodyK points here
     *   ... body ...
     *   [CLOSE base+4]         (only if body locals are captured)
     *   [NLOOP_STEP base]      2 bytes
     *                                   <- exitK points here
     */
    int exitK = MLuaAddConstantRaw(fs, MakeInt(0));
    int bodyK = MLuaAddConstantRaw(fs, MakeInt(0));
    if (exitK < 0 || bodyK < 0 || exitK > 255 || bodyK > 255) {
      Error(p, "too many constants in function");
      return;
    }

    MLuaEmitOpB(fs, OP_LOADK, (U8)exitK);
    StackPush(p, 1);
    MLuaEmitOpB(fs, OP_LOADK, (U8)bodyK);
    StackPush(p, 1);

    /* Stack now has: [start, limit, step, exit_target, body_target] */

    /* NLOOP_PREP: pops 5, stores 4 to locals, pushes start if loop runs */
    MLuaEmitOpB(fs, OP_NLOOP_PREP, (U8)baseSlot);
    StackPop(p, 5);  /* FORPREP consumes 5 values */
    StackPush(p, 1); /* FORPREP pushes start if loop runs */

    /* Body target points here (after FORPREP, before SETLOCAL) */
    Size bodyStart = MLuaCodePos(fs);

    /* Store TOS (current idx) to visible loop variable (at baseSlot+4) */
    MLuaEmitOpB(fs, OP_SETLOCAL, (U8)(baseSlot + 4));
    StackPop(p, 1); /* SETLOCAL consumes the pushed value */

    /* Loop body */
    BeginScope(p, TRUE);
    ParseBlock(p);
    EndScope(p);
    Expect(p, TK_END);

    /* End-of-iteration: close captures of the visible var and body locals
     * so each iteration gets fresh upvalues (Lua semantics) */
    EmitCloseIfCaptured(p, (U8)(baseSlot + 4));

    /* NLOOP_STEP: increments idx, reads body target from local, jumps if
     * continuing */
    MLuaEmitOpB(fs, OP_NLOOP_STEP, (U8)baseSlot);

    /* Exit point: zero-iteration jump, step fallthrough and breaks land here */
    Size exitPos = MLuaCodePos(fs);
    EmitCloseIfCaptured(p, (U8)(baseSlot + 4));
    EmitLocalCleanup(p, (U8)baseSlot, (U8)(baseSlot + 5));

    /* Patch the placeholder constants with the absolute positions */
    fs->Proto->Constants[exitK] = MakeInt((I32)exitPos);
    fs->Proto->Constants[bodyK] = MakeInt((I32)bodyStart);

    PatchAll(p, breakWatermark, exitPos);

    /* Remove all 5 locals */
    fs->LocalsSize -= 5;
    fs->NumLocals -= 5;
  } else if (Check(p, TK_COMMA) || Check(p, TK_IN)) {
    /*
     * Generic for: for k, v in iterator do ... end
     *
     * NEW APPROACH: Use local slots for iterator state.
     *
     * Local slot layout:
     *   slot+0: iterator function (f)
     *   slot+1: state (s)
     *   slot+2: control variable (v)
     *   slot+3..slot+3+nvar-1: loop variables (k, v, ...)
     *
     * Bytecode sequence:
     *   1. Parse iterator expr, store results in local slots via SETLOCAL
     *   2. TFORPREP: jump forward to TFORCALL
     *   3. Loop body
     *   4. JMP back to TFORCALL
     *   5. TFORCALL: call f(s,v), store results on stack
     *   6. TFORLOOP: if nil exit, else update v, copy to loop vars, jump to
     * body
     */
    int varCount = 1;
    U8 iterSlot; /* Local slot where f, s, v starts */

    /* Arrays to store variable names and lengths (max 16 loop vars) */
    const char *varNames[16];
    Size varLens[16];

    /* First variable is already in varName/varLen from before this branch */
    varNames[0] = varName;
    varLens[0] = varLen;

    /* Collect additional variable names */
    while (Match(p, TK_COMMA)) {
      if (!Check(p, TK_NAME)) {
        Error(p, "expected variable name");
        return;
      }
      /* Capture name BEFORE Advance() consumes the token */
      if (varCount < 16) {
        varNames[varCount] = p->Lex.Token.Value.String.Data;
        varLens[varCount] = p->Lex.Token.Value.String.Length;
      }
      Advance(p);
      varCount++;
    }

    if (varCount > 16) {
      Error(p, "too many loop variables (max 16)");
      return;
    }

    Expect(p, TK_IN);

    /* Allocate hidden local slots for iterator state: f, s, v */
    iterSlot = fs->NumLocals;
    AddLocal(p, "(for iterator)", 14); /* f */
    AddLocal(p, "(for state)", 11);    /* s */
    AddLocal(p, "(for control)", 13);  /* v */

    /* Parse iterator expression(s) and store in local slots */
    /* We expect 1-3 expressions (or a single call that returns up to 3) */
    ParseExpr(p);

    /* Store iterator results (f, s, v) to shadow locals */
    if (EndsWithMultret(fs)) {
      /* A call like pairs(t): normalize to exactly 3 values (f, s, v) and
       * store them in reverse order. Stack has [f, s, v] with v on top. */
      MLuaEmitOpB(fs, OP_ADJUST, 3);
      MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 2); /* Store v (control) */
      StackPop(p, 1);
      MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 1); /* Store s (state) */
      StackPop(p, 1);
      MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 0); /* Store f (func) */
      StackPop(p, 1);
    } else {
      /* Single expression for f, need to parse s and v separately */
      MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 0); /* Store f */
      StackPop(p, 1);

      if (Match(p, TK_COMMA)) {
        ParseExpr(p);
        AdjustTo1(p);
        MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 1); /* Store s */
        StackPop(p, 1);
      } else {
        MLuaEmitOp(fs, OP_LOADNIL);
        StackPush(p, 1);
        MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 1); /* Store nil as s */
        StackPop(p, 1);
      }

      if (Match(p, TK_COMMA)) {
        ParseExpr(p);
        AdjustTo1(p);
        MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 2); /* Store v */
        StackPop(p, 1);
      } else {
        MLuaEmitOp(fs, OP_LOADNIL);
        StackPush(p, 1);
        MLuaEmitOpB(fs, OP_SETLOCAL, iterSlot + 2); /* Store nil as v */
        StackPop(p, 1);
      }
    }

    /* Reserve body_target and var-count slots BEFORE loop variables */
    /* Layout: f/s/v at iterSlot+0/1/2, body_target at +3, var count at +4,
     * loop vars at iterSlot+5+ */
    AddLocal(p, "(for body_target)", 17);
    Size bodyTargetSlot = iterSlot + 3;
    AddLocal(p, "(for nvars)", 11);
    Size nvarsSlot = iterSlot + 4;

    /* Store the loop variable count (static) for GLOOP_STEP normalization */
    MLuaEmitOpB(fs, OP_LOADINT, (U8)(I8)varCount);
    StackPush(p, 1);
    MLuaEmitOpB(fs, OP_SETLOCAL, (U8)nvarsSlot);
    StackPop(p, 1);

    /* Allocate local slots for loop variables with their actual names */
    {
      int i;
      for (i = 0; i < varCount; i++) {
        AddLocal(p, varNames[i], varLens[i]);
      }
    }

    Expect(p, TK_DO);

    /*
     * Generic for-loop structure:
     *   f/s/v stored in Locals[iterSlot..iterSlot+2]
     *   body_target in Locals[iterSlot+3]
     *   loop vars (k, v) in Locals[iterSlot+4..]
     *
     * Loop head:
     *   GLOOP_CALL: Push Func, State, Control from locals
     *   CALL 2: Call iterator(state, control)
     *   GLOOP_STEP: Check nil, update control, jump to body or exit
     *   Body:
     *     SETLOCAL for k, v
     *     ... user code ...
     *     JMP to loop head
     */

    /* Loop head position */
    Size loopHead = MLuaCodePos(fs);

    /* Store body target PC in the local slot. Pushed through a placeholder
     * constant (patched below) so positions beyond 127 work. */
    int bodyK = MLuaAddConstantRaw(fs, MakeInt(0));
    if (bodyK < 0 || bodyK > 255) {
      Error(p, "too many constants in function");
      return;
    }
    MLuaEmitOpB(fs, OP_LOADK, (U8)bodyK);
    StackPush(p, 1);
    MLuaEmitOpB(fs, OP_SETLOCAL, (U8)bodyTargetSlot);
    StackPop(p, 1);

    /* GLOOP_CALL: Push Func, State, Control for iterator call */
    MLuaEmitOpB(fs, OP_GLOOP_CALL, (U8)iterSlot);
    StackPush(p, 3); /* Pushes 3 values */

    /* CALL: Call iterator with 2 args, expect variable results */
    MLuaEmitOpB(fs, OP_CALL, 2); /* 2 args (state, control) */
    StackPop(p, 3);              /* Pop func + 2 args */
    StackPush(p, varCount);      /* Push expected results (k, v, ...) */

    /* GLOOP_STEP: Check first result, if nil fallthrough, else jump to body */
    MLuaEmitOpB(fs, OP_GLOOP_STEP, (U8)iterSlot);

    /* JMP to exit - this is reached when GLOOP_STEP falls through (nil case) */
    /* Will be patched after we know the exit position */
    MLuaFwdJump jmpToExit = EmitFwdJump(p, OP_JMP);
    StackPop(p,
             varCount); /* On fallthrough, stack is empty (results cleared) */

    /* Body start - GLOOP_STEP jumps here on success */
    Size bodyStart = MLuaCodePos(fs);

    /* Store results to user loop variables (k, v, ...) */
    /* Results are on stack in order, store to loop var locals */
    {
      int i;
      Size loopVarBase = iterSlot + 5; /* After f, s, v, body_target, nvars */
      for (i = varCount - 1; i >= 0; i--) {
        MLuaEmitOpB(fs, OP_SETLOCAL, (U8)(loopVarBase + i));
        StackPop(p, 1);
      }
    }

    /* Loop body */
    BeginScope(p, TRUE);
    ParseBlock(p);
    EndScope(p);
    Expect(p, TK_END);

    /* End-of-iteration: close captures of loop vars and body locals */
    EmitCloseIfCaptured(p, (U8)(iterSlot + 5));

    /* Jump back to loop head (the body-target store) */
    EmitBackJump(p, OP_JMP, loopHead);

    /* Exit point - nil-fallthrough jump and breaks land here */
    Size exitPos = MLuaCodePos(fs);
    EmitCloseIfCaptured(p, (U8)(iterSlot + 5));
    EmitLocalCleanup(p, iterSlot, (U8)(iterSlot + 5 + varCount));

    /* Patch JMP-to-exit instruction to jump here */
    PatchFwdJump(p, jmpToExit, exitPos);

    /* Patch the body-target placeholder constant */
    fs->Proto->Constants[bodyK] = MakeInt((I32)bodyStart);

    PatchAll(p, breakWatermark, exitPos);

    /* Pop iterator state and loop variables from locals */
    fs->NumLocals -= (5 + varCount); /* f, s, v, body_target, nvars + vars */
    fs->LocalsSize -= (5 + varCount);
  } else {
    Error(p, "expected '=' or 'in' after for variable");
  }
}

/* ========================================================================== */
/* Function Declarations                                                      */
/* ========================================================================== */

static void ParseFunction(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  const char *baseName;
  Size baseNameLen;
  const char *fieldName = NULL;
  Size fieldNameLen = 0;
  int baseK, fieldK;
  Bool isMethod = FALSE;
  Bool isTableMethod = FALSE;
  int protoIdx;

  Advance(p); /* Skip 'function' */

  if (!Check(p, TK_NAME)) {
    Error(p, "expected function name");
    return;
  }

  /* Get base table/variable name */
  baseName = p->Lex.Token.Value.String.Data;
  baseNameLen = p->Lex.Token.Value.String.Length;
  Advance(p);

  /* Handle table.field or table:method syntax */
  while (Check(p, TK_DOT) || Check(p, TK_COLON)) {
    isTableMethod = TRUE;
    if (Check(p, TK_COLON)) {
      isMethod = TRUE;
    }
    Advance(p);
    if (!Check(p, TK_NAME)) {
      Error(p, "expected field name");
      return;
    }
    /* The new name becomes the field, previous becomes table part */
    if (fieldName) {
      /* Multiple levels: push previous field onto table, get next */
      /* For now, handle single-level table.field only */
    }
    fieldName = p->Lex.Token.Value.String.Data;
    fieldNameLen = p->Lex.Token.Value.String.Length;
    Advance(p);
  }

  /* Parse function body and get proto index */
  protoIdx = ParseFuncBody(p, isMethod);
  if (protoIdx < 0) {
    return; /* Error already reported */
  }

  if (isTableMethod) {
    /* Get the base table and set field */
    fieldK = MLuaAddStringK(fs, fieldName, fieldNameLen);

    /* Base table may be a local, an upvalue, or a global */
    EmitGetVar(p, baseName, baseNameLen);
    StackPush(p, 1);

    /* Emit OP_CLOSURE */
    MLuaEmitOpB(fs, OP_CLOSURE, (U8)protoIdx);
    StackPush(p, 1);

    /* Set field: Stack has [table, closure]. SETTABLE leaves the table
     * (constructors rely on that), so drop it explicitly. */
    MLuaEmitOpB(fs, OP_LOADK, (U8)(U16)fieldK);
    MLuaEmitOp(fs, OP_SWAP);
    MLuaEmitOp(fs, OP_SETTABLE_POP);
    StackPop(p, 2);
  } else {
    /* Simple global function */
    baseK = MLuaAddStringK(fs, baseName, baseNameLen);

    /* Emit OP_CLOSURE with proto index */
    MLuaEmitOpB(fs, OP_CLOSURE, (U8)protoIdx);
    StackPush(p, 1);

    /* Assign closure to global */
    MLuaEmitOpB(fs, OP_LOADK, (U8)baseK);
    MLuaEmitOp(fs, OP_SWAP);
    MLuaEmitOp(fs, OP_SETGLOBAL);
    StackPop(p, 1);
  }
}

/*
 * Parse function body and create nested proto.
 * This creates a new function state, parses the body, and returns
 * the index of the nested proto in the parent's protos array.
 */
static int ParseFuncBody(MLuaParser *p, Bool isMethod) {
  MLuaFuncState *outerFS = p->FS;
  MLuaFuncState fs;
  MLuaProto *proto;
  Size protoIdx;

  /* Initialize new function state */
  MemSet(&fs, 0, sizeof(MLuaFuncState));
  fs.L = p->L;
  fs.Proto = MLuaProtoNew(p->L);
  fs.Prev = outerFS;
  fs.StackLevel = 0;
  fs.MaxStack = 0;
  fs.MaxCapturedSlot = -1;
  fs.LastInstrPos = (Size)-1;
  fs.PrevInstrPos = (Size)-1;

  if (!fs.Proto) {
    Error(p, "out of memory for function");
    return -1;
  }

  proto = fs.Proto;

  /* Inherit source from parent for stacktraces */
  if (outerFS && outerFS->Proto) {
    proto->Source = outerFS->Proto->Source;
  }

  /* Switch to new function state */
  p->FS = &fs;

  /* Parse parameter list */
  Expect(p, TK_LPAREN);

  /* Add implicit 'self' for methods (AddLocal already bumps NumLocals) */
  if (isMethod) {
    AddLocal(p, "self", 4);
    proto->NumParams++;
  }

  /* Parse parameters */
  while (!Check(p, TK_RPAREN) && !Check(p, TK_EOF)) {
    if (Check(p, TK_NAME)) {
      const char *paramName = p->Lex.Token.Value.String.Data;
      Size paramLen = p->Lex.Token.Value.String.Length;
      AddLocal(p, paramName, paramLen);
      proto->NumParams++;
      Advance(p);
    } else if (Check(p, TK_DOTS)) {
      proto->IsVararg = 1;
      Advance(p);
      break;
    } else {
      Error(p, "expected parameter name");
      break;
    }
    if (!Match(p, TK_COMMA)) {
      break;
    }
  }
  Expect(p, TK_RPAREN);

  /* Parse function body */
  ParseBlock(p);
  Expect(p, TK_END);

  /* Emit return if not already present */
  MLuaEmitOpB(&fs, OP_RET, 0);

  if (fs.CodeOverflow) {
    Error(p, "function too large");
    p->FS = outerFS;
    return -1;
  }
  if (fs.MaxStack > 255) {
    Error(p, "expression too complex");
    p->FS = outerFS;
    return -1;
  }
  proto->MaxStackSize = (U8)(fs.MaxStack > 2 ? fs.MaxStack : 2);
  proto->NumLocals =
      fs.MaxLocals; /* Reserve the PEAK slot count (loops release slots) */
  OptimizeLastLocalUses(&fs);

  /* A short-jump overflow anywhere triggers the long-jump re-parse of the
   * whole chunk: bubble the flag to the top-level FuncState. */
  if (fs.JumpOverflow) {
    outerFS->JumpOverflow = TRUE;
  }

  /* Copy compile-time upvalue descriptors into the prototype */
  if (fs.UpvalsSize > 0) {
    Size i;
    proto->Upvalues =
        (MLuaUpvalDesc *)MLuaAlloc(p->L, fs.UpvalsSize * sizeof(MLuaUpvalDesc));
    if (!proto->Upvalues) {
      Error(p, "out of memory");
      p->FS = outerFS;
      return -1;
    }
    for (i = 0; i < fs.UpvalsSize; i++) {
      proto->Upvalues[i].InStack = fs.Upvals[i].InStack;
      proto->Upvalues[i].Index = fs.Upvals[i].Index;
    }
    proto->UpvaluesSize = fs.UpvalsSize;
  }

  /* Add proto to parent's nested protos */
  if (outerFS->Proto->ProtosSize >= 255) {
    Error(p, "too many nested functions");
    p->FS = outerFS;
    return -1;
  }

  /* Grow protos array if needed */
  {
    Size newSize = outerFS->Proto->ProtosSize + 1;
    MLuaProto **newProtos =
        (MLuaProto **)MLuaAlloc(p->L, newSize * sizeof(MLuaProto *));
    if (!newProtos) {
      Error(p, "out of memory");
      p->FS = outerFS;
      return -1;
    }
    if (outerFS->Proto->Protos) {
      MemCpy(newProtos, outerFS->Proto->Protos,
             outerFS->Proto->ProtosSize * sizeof(MLuaProto *));
    }
    outerFS->Proto->Protos = newProtos;
  }

  protoIdx = outerFS->Proto->ProtosSize;
  outerFS->Proto->Protos[protoIdx] = proto;
  outerFS->Proto->ProtosSize++;

  /* Restore outer function state */
  p->FS = outerFS;

  return (int)protoIdx;
}

/* ========================================================================== */
/* Break Statement                                                            */
/* ========================================================================== */

static void ParseBreak(MLuaParser *p) {
  Advance(p); /* Skip 'break' */

  /* Emit jump with placeholder; the enclosing loop patches it to its exit
   * point (which also closes captured loop locals). */
  EmitBreakCleanup(p);
  PushPatch(p, EmitFwdJump(p, OP_JMP), TRUE);
}

/*
 * Multiple assignment: t1, t2, ... = e1, e2, ...
 *
 * Index targets evaluate their [table, key] pair up front (stacked in
 * declaration order). The value list is normalized to exactly N values,
 * stashed in hidden temp locals, then stores run LAST target first so each
 * index target's pair surfaces at the stack top exactly when needed.
 * Temps also give `a, b = b, a` correct swap semantics.
 */
#define MAX_ASSIGN_TARGETS 8

static void ParseMultiAssign(MLuaParser *p, AccessInfo firstInfo,
                             const char *firstName, Size firstNameLen) {
  MLuaFuncState *fs = p->FS;
  enum { T_LOCAL, T_UPVAL, T_GLOBAL, T_INDEX };
  struct {
    U8 Kind;
    U8 Idx;    /* local slot / upvalue index */
    int NameK; /* global name constant */
  } targets[MAX_ASSIGN_TARGETS];
  int n = 0;
  int values = 0;
  U8 tempBase;
  int i;

  /* Normalize the already-parsed first target */
  if (firstInfo.accessType == 1) {
    targets[n++].Kind = T_INDEX; /* [t, k] already on the stack */
  } else if (firstInfo.accessType == 2) {
    MLuaEmitOpB(fs, OP_LOADK, (U8)firstInfo.fieldConstIdx); /* t -> [t, k] */
    StackPush(p, 1);
    targets[n++].Kind = T_INDEX;
  } else if (firstName) {
    int idx;
    /* The name's value was pushed by ParsePrefix and is unused: drop it */
    MLuaEmitOpB(fs, OP_POP, 1);
    StackPop(p, 1);
    switch (ResolveVar(p, fs, firstName, firstNameLen, &idx)) {
    case VAR_LOCAL:
      targets[n].Kind = T_LOCAL;
      targets[n].Idx = (U8)idx;
      break;
    case VAR_UPVAL:
      targets[n].Kind = T_UPVAL;
      targets[n].Idx = (U8)idx;
      break;
    default:
      targets[n].Kind = T_GLOBAL;
      targets[n].NameK = MLuaAddStringK(fs, firstName, firstNameLen);
      break;
    }
    n++;
  } else {
    Error(p, "invalid assignment target");
    return;
  }

  /* Remaining targets */
  while (Match(p, TK_COMMA)) {
    const char *tname;
    Size tlen;

    if (n >= MAX_ASSIGN_TARGETS) {
      Error(p, "too many assignment targets (max 8)");
      return;
    }
    if (!Check(p, TK_NAME)) {
      Error(p, "expected assignment target");
      return;
    }
    tname = p->Lex.Token.Value.String.Data;
    tlen = p->Lex.Token.Value.String.Length;
    Advance(p);

    if (Check(p, TK_DOT) || Check(p, TK_LBRACKET)) {
      /* Indexed target: evaluate the access chain, leaving [t, k] */
      EmitGetVar(p, tname, tlen);
      StackPush(p, 1);
      for (;;) {
        if (Match(p, TK_DOT)) {
          int k;
          if (!Check(p, TK_NAME)) {
            Error(p, "expected field name");
            return;
          }
          k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                             p->Lex.Token.Value.String.Length);
          Advance(p);
          MLuaEmitOpB(fs, OP_LOADK, (U8)k);
          StackPush(p, 1);
          if (Check(p, TK_DOT) || Check(p, TK_LBRACKET)) {
            MLuaEmitOp(fs, OP_GETTABLE); /* Intermediate access */
            StackPop(p, 1);
          } else {
            break; /* Final: [t, k] stays */
          }
        } else if (Match(p, TK_LBRACKET)) {
          ParseExpr(p);
          AdjustTo1(p);
          Expect(p, TK_RBRACKET);
          if (Check(p, TK_DOT) || Check(p, TK_LBRACKET)) {
            MLuaEmitOp(fs, OP_GETTABLE);
            StackPop(p, 1);
          } else {
            break; /* Final: [t, k] stays */
          }
        } else {
          Error(p, "expected assignment target");
          return;
        }
      }
      targets[n++].Kind = T_INDEX;
    } else {
      int idx;
      switch (ResolveVar(p, fs, tname, tlen, &idx)) {
      case VAR_LOCAL:
        targets[n].Kind = T_LOCAL;
        targets[n].Idx = (U8)idx;
        break;
      case VAR_UPVAL:
        targets[n].Kind = T_UPVAL;
        targets[n].Idx = (U8)idx;
        break;
      default:
        targets[n].Kind = T_GLOBAL;
        targets[n].NameK = MLuaAddStringK(fs, tname, tlen);
        break;
      }
      n++;
    }
  }

  Expect(p, TK_ASSIGN);

  /* Value list, normalized to exactly n values */
  for (;;) {
    ParseExpr(p);
    values++;
    if (!Match(p, TK_COMMA)) {
      break;
    }
    AdjustTo1(p);
  }
  {
    Bool multret = EndsWithMultret(fs);
    if (values <= n) {
      int missing = n - values;
      if (multret) {
        MLuaEmitOpB(fs, OP_ADJUST, (U8)(missing + 1));
        StackPush(p, missing);
      } else {
        for (i = 0; i < missing; i++) {
          MLuaEmitOp(fs, OP_LOADNIL);
          StackPush(p, 1);
        }
      }
    } else {
      if (multret) {
        MLuaEmitOpB(fs, OP_ADJUST, 1);
      }
      MLuaEmitOpB(fs, OP_POP, (U8)(values - n));
      StackPop(p, values - n);
    }
  }

  /* Stash the values into hidden temp locals (popped in reverse) */
  tempBase = fs->NumLocals;
  for (i = 0; i < n; i++) {
    AddLocal(p, "(assign tmp)", 12);
  }
  for (i = n - 1; i >= 0; i--) {
    MLuaEmitOpB(fs, OP_SETLOCAL, (U8)(tempBase + i));
    StackPop(p, 1);
  }

  /* Store back, LAST target first */
  for (i = n - 1; i >= 0; i--) {
    MLuaEmitOpB(fs, OP_GETLOCAL, (U8)(tempBase + i));
    switch (targets[i].Kind) {
    case T_LOCAL:
      MLuaEmitOpB(fs, OP_SETLOCAL, targets[i].Idx);
      break;
    case T_UPVAL:
      MLuaEmitOpB(fs, OP_SETUPVAL, targets[i].Idx);
      break;
    case T_GLOBAL:
      MLuaEmitOpB(fs, OP_LOADK, (U8)targets[i].NameK);
      MLuaEmitOp(fs, OP_SWAP);
      MLuaEmitOp(fs, OP_SETGLOBAL);
      break;
    case T_INDEX:
      /* Stack: [..., t, k, v]; SETTABLE_POP also drops t */
      MLuaEmitOp(fs, OP_SETTABLE_POP);
      StackPop(p, 2);
      break;
    }
  }

  /* Release the temp slots (frame space stays reserved via MaxLocals) */
  fs->NumLocals -= (U8)n;
  fs->LocalsSize -= (Size)n;
}

static void ParseExprStat(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  const char *name = NULL;
  Size nameLen = 0;
  AccessInfo accessInfo;

  /* Save name if this is a simple name start */
  if (Check(p, TK_NAME)) {
    name = p->Lex.Token.Value.String.Data;
    nameLen = p->Lex.Token.Value.String.Length;
  }

  /* Parse primary expression (name, literal, etc) */
  ParsePrefix(p);

  /* Parse suffixes, potentially leaving a pending table access */
  accessInfo = ParseSuffixForAssign(p);

  if (Check(p, TK_COMMA)) {
    /* Multiple assignment: a, b, ... = ... */
    ParseMultiAssign(p, accessInfo, name, nameLen);
    return;
  }

  if (Check(p, TK_ASSIGN)) {
    /* Assignment */
    if (accessInfo.accessType == 1) {
      /* t[k] = v => Stack has: [t, k], consume =, parse value, emit the
       * store. When t and k are two just-emitted local loads the pair is
       * retracted BEFORE the value is parsed (so nothing the value
       * records can be invalidated) and one SETTABLE_LL does the store
       * with neither crossing the stack.
       *
       * SETTABLE_LL reads its slots at store time, i.e. AFTER the value
       * ran. That only diverges from the unfused order when the value
       * can mutate them, which requires a closure capturing the slot
       * (open upvalues write through to the Locals array). Slots already
       * captured are refused up front; a capture that first appears
       * inside the value is repaired by reinserting the two loads ahead
       * of the value's code so both are read before it runs. */
      U8 slots = 0;
      Size valueStart;
      Bool fused = TryRetractLocalPair(fs, &slots);
      if (fused && (IsCapturedLocal(fs, slots >> 4) ||
                    IsCapturedLocal(fs, slots & 0x0F))) {
        MLuaEmitOpB(fs, OP_GETLOCAL, (U8)(slots >> 4));
        MLuaEmitOpB(fs, OP_GETLOCAL, (U8)(slots & 0x0F));
        fused = FALSE;
      }
      valueStart = MLuaCodePos(fs);
      Advance(p);
      ParseExpr(p);
      AdjustTo1(p);
      if (fused && (IsCapturedLocal(fs, slots >> 4) ||
                    IsCapturedLocal(fs, slots & 0x0F))) {
        /* The value captured t or k: restore the read-before-value
         * order by inserting the loads ahead of the value's code. */
        InsertLocalPairAt(fs, valueStart, slots);
        fused = FALSE;
      }
      if (fused) {
        MLuaEmitOpB(fs, OP_SETTABLE_LL, slots);
      } else {
        MLuaEmitOp(fs, OP_SETTABLE_POP);
      }
      StackPop(p, 3); /* Pop t, k, v */
    } else if (accessInfo.accessType == 2) {
      /* t.field = v => Stack has: [t], consume =, parse value, emit the
       * store (SETTABLE_POP also drops the table). */
      Advance(p);
      ParseExpr(p);
      AdjustTo1(p);
      MLuaEmitOpB(fs, OP_LOADK, (U8)accessInfo.fieldConstIdx);
      MLuaEmitOp(fs, OP_SWAP);
      MLuaEmitOp(fs, OP_SETTABLE_POP);
      StackPop(p, 2); /* Pop t, v */
    } else if (name) {
      /* Simple name = value - ParseAssignment expects to see and consume = */
      MLuaEmitOpB(fs, OP_POP, 1);
      StackPop(p, 1);
      ParseAssignment(p, name, nameLen);
    } else {
      Error(p, "invalid assignment target");
      MLuaEmitOpB(fs, OP_POP, 1);
      StackPop(p, 1);
    }
  } else {
    /* Expression statement (function call) - complete any pending access */
    EmitPendingGet(p, &accessInfo);
    /* Discard the statement's value(s): a trailing call may have produced
     * any number of results */
    if (EndsWithMultret(fs)) {
      MLuaEmitOpB(fs, OP_ADJUST, 0);
    } else {
      MLuaEmitOpB(fs, OP_POP, 1);
    }
    StackPop(p, 1);
  }
}

static void ParseStatement(MLuaParser *p) {
  /* Record line info for this statement */
  EMIT_LINE(p);

  switch (p->Lex.Token.Type) {
  case TK_SEMICOLON:
    Advance(p);
    break;

  case TK_LOCAL:
    ParseLocal(p);
    break;

  case TK_RETURN:
    ParseReturn(p);
    break;

  case TK_IF:
    ParseIf(p);
    break;

  case TK_WHILE:
    ParseWhile(p);
    break;

  case TK_REPEAT:
    ParseRepeat(p);
    break;

  case TK_DO:
    Advance(p);
    BeginScope(p, FALSE);
    ParseBlock(p);
    EndScope(p);
    Expect(p, TK_END);
    break;

  case TK_FOR:
    ParseFor(p);
    break;

  case TK_FUNCTION:
    ParseFunction(p);
    break;

  case TK_BREAK:
    ParseBreak(p);
    break;

  default:
    ParseExprStat(p);
    break;
  }
}

static void ParseBlock(MLuaParser *p) {
  if (!EnterParseDepth(p)) {
    return;
  }

  while (!p->Error && !Check(p, TK_END) && !Check(p, TK_ELSE) &&
         !Check(p, TK_ELSEIF) && !Check(p, TK_UNTIL) && !Check(p, TK_EOF)) {
    ParseStatement(p);
    Match(p, TK_SEMICOLON);
  }

  LeaveParseDepth(p);
}

static void ParseChunk(MLuaParser *p) {
  ParseBlock(p);

  /* Implicit return */
  MLuaEmitOpB(p->FS, OP_RET, 0);
}

/* ========================================================================== */
/* Main Parse Function                                                        */
/* ========================================================================== */

static MLuaProto *ParseOnce(MLuaState *L, const char *source, Size len,
                            const char *name, Bool longJumps,
                            Bool *outOverflow, const char **outError) {
  MLuaParser parser;
  MLuaFuncState fs;
  MLuaProto *proto;

  MemSet(&parser, 0, sizeof(MLuaParser));
  MemSet(&fs, 0, sizeof(MLuaFuncState));

  parser.L = L;
  parser.FS = &fs;
  parser.Error = NULL;
  parser.LongJumps = longJumps;

  fs.L = L;
  fs.Proto = MLuaProtoNew(L);
  fs.Prev = NULL;
  fs.StackLevel = 0;
  fs.MaxStack = 0;
  fs.MaxCapturedSlot = -1;
  fs.LastInstrPos = (Size)-1;
  fs.PrevInstrPos = (Size)-1;

  if (!fs.Proto) {
    *outError = "out of memory";
    return NULL;
  }

  proto = fs.Proto;
  proto->MaxStackSize = 2; /* Minimum */

  /* Set source name for stacktraces */
  if (name && name[0] != '\0') {
    proto->Source = MLuaStringNew(L, name, StrLen(name));
  }

  MLuaLexInit(&parser.Lex, L, source, len);
  MLuaLexNext(&parser.Lex); /* Get first token */

  ParseChunk(&parser);

  *outOverflow = fs.JumpOverflow;

  if (parser.Error) {
    *outError = parser.Error;
    return NULL;
  }

  if (fs.CodeOverflow) {
    *outError = "function too large";
    return NULL;
  }
  if (fs.MaxStack > 255) {
    *outError = "expression too complex";
    return NULL;
  }
  proto->MaxStackSize = (U8)(fs.MaxStack > 2 ? fs.MaxStack : 2);
  proto->NumLocals =
      fs.MaxLocals; /* Reserve the PEAK slot count (loops release slots) */
  OptimizeLastLocalUses(&fs);

  return proto;
}

MLuaProto *MLuaParse(MLuaState *L, const char *source, Size len,
                     const char *name) {
  const char *error = NULL;
  Bool overflow = FALSE;
  MLuaProto *proto;

  /* First pass: compact short jumps (I8 offsets). */
  proto = ParseOnce(L, source, len, name, FALSE, &overflow, &error);

  /* If any forward jump exceeded +/-127, re-parse the whole chunk with
   * long-form jumps (offset via constant + JMP_S). The discarded first
   * attempt is reclaimed by the GC. */
  if (!error && overflow) {
    proto = ParseOnce(L, source, len, name, TRUE, &overflow, &error);
    if (!error && overflow) {
      /* Long jumps cannot overflow; this would be a compiler bug. */
      error = "internal: jump overflow in long-jump mode";
      proto = NULL;
    }
  }

  if (error) {
    L->ErrorMsg = error;
    return NULL;
  }

  return proto;
}

const char *MLuaParseError(MLuaParser *p) { return p->Error; }
