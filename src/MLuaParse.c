/*
 * MicroLua - MLuaParse.c
 * Pratt parser with stack-based codegen
 */

#include "MLuaParse.h"
#include "MLuaString.h"
#include <stdio.h>

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

static void Error(MLuaParser *p, const char *msg) {
  if (!p->Error) {
    p->Error = msg;
    p->ErrorLine = p->Lex.Line;
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
    if (outer > fs->Prev->MaxCapturedSlot) {
      fs->Prev->MaxCapturedSlot = outer;
    }
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
static void PushPatch(MLuaParser *p, MLuaFwdJump j) {
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
    MLuaEmitOpB(fs, OP_LOADK, (U8)k);
    MLuaEmitOp(fs, OP_GETGLOBAL);
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

static void ParsePrimaryExpr(MLuaParser *p);
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
    Advance(p);

    /* Check if fits in 8-bit signed int */
    if (num == (double)(I32)num && (I32)num >= -128 && (I32)num <= 127) {
      MLuaEmitOpB(fs, OP_LOADINT, (U8)(I8)(I32)num);
    } else {
      /* Add to constant pool */
      int k = MLuaAddConstant(fs, MakeInt((I32)num));
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
          Expect(p, TK_RBRACKET);
          Expect(p, TK_ASSIGN);
          ParseExpr(p); /* value */
          MLuaEmitOp(fs, OP_SETTABLE);
          StackPop(p, 2);
        } else if (Check(p, TK_NAME) && MLuaLexPeek(&p->Lex) == TK_ASSIGN) {
          /* name = value */
          int k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                                 p->Lex.Token.Value.String.Length);
          Advance(p); /* name */
          Advance(p); /* = */
          ParseExpr(p);
          MLuaEmitOpB(fs, OP_LOADK, (U8)(U16)k);
          MLuaEmitOp(fs, OP_SWAP);
          MLuaEmitOp(fs, OP_SETTABLE);
          StackPop(p, 1);
        } else {
          /* Array element */
          ParseExpr(p);
          MLuaEmitOp(fs, OP_APPEND);
          StackPop(p, 1);
          count++;
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
    MLuaEmitOp(fs, OP_UNM);
    break;

  case TK_NOT:
    Advance(p);
    ParseSubExpr(p, PREC_UNARY);
    MLuaEmitOp(fs, OP_NOT);
    break;

  case TK_HASH:
    Advance(p);
    ParseSubExpr(p, PREC_UNARY);
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
    /* Emit OP_VARARG to push varargs to stack */
    /* For now, push 1 value (first vararg) */
    MLuaEmitOpB(fs, OP_VARARG, 1);
    StackPush(p, 1);
    break;
  }

  default:
    Error(p, "unexpected token in expression");
    /* Push nil as fallback */
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

static AccessInfo ParseSuffixForAssign(MLuaParser *p) {
  MLuaFuncState *fs = p->FS;
  AccessInfo info = {0, 0};

  for (;;) {
    switch (p->Lex.Token.Type) {
    case TK_DOT: {
      /* .field - complete any pending access first */
      if (info.accessType == 1) {
        MLuaEmitOp(fs, OP_GETTABLE);
        StackPop(p, 1);
      } else if (info.accessType == 2) {
        MLuaEmitOpB(fs, OP_LOADK, (U8)info.fieldConstIdx);
        MLuaEmitOp(fs, OP_GETTABLE);
      }

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
      if (info.accessType == 1) {
        MLuaEmitOp(fs, OP_GETTABLE);
        StackPop(p, 1);
      } else if (info.accessType == 2) {
        MLuaEmitOpB(fs, OP_LOADK, (U8)info.fieldConstIdx);
        MLuaEmitOp(fs, OP_GETTABLE);
      }

      Advance(p);
      ParseExpr(p);
      Expect(p, TK_RBRACKET);
      info.accessType = 1; /* table[key] access */
      info.fieldConstIdx = 0;
      break;
    }

    case TK_LPAREN: {
      /* Function call - complete pending access */
      if (info.accessType == 1) {
        MLuaEmitOp(fs, OP_GETTABLE);
        StackPop(p, 1);
      } else if (info.accessType == 2) {
        MLuaEmitOpB(fs, OP_LOADK, (U8)info.fieldConstIdx);
        MLuaEmitOp(fs, OP_GETTABLE);
      }
      info.accessType = 0;

      int argCount = 0;
      Advance(p);
      while (!Check(p, TK_RPAREN) && !Check(p, TK_EOF)) {
        ParseExpr(p);
        argCount++;
        if (!Match(p, TK_COMMA))
          break;
      }
      Expect(p, TK_RPAREN);
      MLuaEmitOpB(fs, OP_CALL, (U8)argCount);
      StackPop(p, argCount);
      break;
    }

    case TK_STRING: {
      /* f"string" - complete pending access */
      if (info.accessType == 1) {
        MLuaEmitOp(fs, OP_GETTABLE);
        StackPop(p, 1);
      } else if (info.accessType == 2) {
        MLuaEmitOpB(fs, OP_LOADK, (U8)info.fieldConstIdx);
        MLuaEmitOp(fs, OP_GETTABLE);
      }
      info.accessType = 0;

      int k = MLuaAddStringK(fs, p->Lex.Token.Value.String.Data,
                             p->Lex.Token.Value.String.Length);
      Advance(p);
      MLuaEmitOpB(fs, OP_LOADK, (U8)k);
      StackPush(p, 1);
      MLuaEmitOpB(fs, OP_CALL, 1);
      StackPop(p, 1);
      break;
    }

    case TK_LBRACE: {
      /* f{table} - complete pending access */
      if (info.accessType == 1) {
        MLuaEmitOp(fs, OP_GETTABLE);
        StackPop(p, 1);
      } else if (info.accessType == 2) {
        MLuaEmitOpB(fs, OP_LOADK, (U8)info.fieldConstIdx);
        MLuaEmitOp(fs, OP_GETTABLE);
      }
      info.accessType = 0;

      ParsePrefix(p);
      MLuaEmitOpB(fs, OP_CALL, 1);
      StackPop(p, 1);
      break;
    }

    case TK_COLON: {
      /* Method call: t:name(args) — sugar for t.name(t, args) */
      int methodK;
      int argCount = 1; /* implicit self */

      /* Complete pending access so the receiver is at TOS */
      if (info.accessType == 1) {
        MLuaEmitOp(fs, OP_GETTABLE);
        StackPop(p, 1);
      } else if (info.accessType == 2) {
        MLuaEmitOpB(fs, OP_LOADK, (U8)info.fieldConstIdx);
        MLuaEmitOp(fs, OP_GETTABLE);
      }
      info.accessType = 0;

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

      MLuaEmitOpB(fs, OP_CALL, (U8)argCount);
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
  if (info.accessType == 1) {
    MLuaEmitOp(p->FS, OP_GETTABLE);
    StackPop(p, 1);
  } else if (info.accessType == 2) {
    MLuaEmitOpB(p->FS, OP_LOADK, (U8)info.fieldConstIdx);
    MLuaEmitOp(p->FS, OP_GETTABLE);
  }
}

static void ParseSubExpr(MLuaParser *p, Precedence minPrec) {
  MLuaFuncState *fs = p->FS;

  ParsePrefix(p);
  ParseSuffix(p);

  while (GetPrecedence(p->Lex.Token.Type) > minPrec ||
         (GetPrecedence(p->Lex.Token.Type) == minPrec &&
          IsRightAssoc(p->Lex.Token.Type))) {
    MLuaTokenType op = p->Lex.Token.Type;
    Precedence prec = GetPrecedence(op);
    Precedence nextPrec = IsRightAssoc(op) ? prec : (Precedence)(prec + 1);

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
      PatchFwdJump(p, jmp, MLuaCodePos(fs));
    } else if (op == TK_CONCAT) {
      /* Collect all concatenations */
      int count = 1;
      Advance(p);
      ParseSubExpr(p, nextPrec);
      count++;

      while (Check(p, TK_CONCAT)) {
        Advance(p);
        ParseSubExpr(p, nextPrec);
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

      /* a > b  ==  b < a: with [a, b] on the stack, SWAP yields [b, a]
       * and OP_LT then computes b < a. Same for >= via OP_LE. */
      if (swap) {
        MLuaEmitOp(fs, OP_SWAP);
      }

      MLuaEmitOp(fs, binOp);
      StackPop(p, 1); /* Two operands -> one result */
    }
  }
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
  int lastExprIsCall = 0; /* Track if last expr was a function call */
  if (Match(p, TK_ASSIGN)) {
    do {
      /* Check if this is a function call by looking at suffix */
      Size savedPos = MLuaCodePos(fs);
      ParseExpr(p);
      /* If the expression generated an OP_CALL, it's a function call */
      /* Simple heuristic: check if the last opcode is OP_CALL (0x71) */
      if (MLuaCodePos(fs) > savedPos) {
        Size lastPos = MLuaCodePos(fs) - 1;
        /* OP_CALL is 2 bytes: opcode + nargs, so check position - 2 */
        if (lastPos >= 1) {
          U8 lastOp = fs->Proto->Code[lastPos - 1];
          lastExprIsCall = (lastOp == OP_CALL);
        }
      }
      exprCount++;
    } while (Match(p, TK_COMMA) && exprCount < varCount);
  }

  /* Assign values to locals (in reverse to match stack order) */
  {
    int i;
    /* Fill missing values with nil - BUT skip if last expr was a call */
    /* Function calls can return multiple values at runtime */
    if (!lastExprIsCall) {
      for (i = exprCount; i < varCount; i++) {
        MLuaEmitOp(fs, OP_LOADNIL);
        StackPush(p, 1);
        exprCount++;
      }
    } else {
      /* For function calls, assume it returns enough values */
      /* Adjust exprCount to match varCount so we pop the right number */
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
    do {
      ParseExpr(p);
      retCount++;
    } while (Match(p, TK_COMMA));
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
  Expect(p, TK_THEN);

  jmpToElse = EmitFwdJump(p, OP_JMPF);
  StackPop(p, 1);

  ParseBlock(p);

  while (Check(p, TK_ELSEIF)) {
    PushPatch(p, EmitFwdJump(p, OP_JMP)); /* Arm done -> jump to end */

    PatchFwdJump(p, jmpToElse, MLuaCodePos(fs));
    Advance(p); /* Skip 'elseif' */

    ParseExpr(p);
    Expect(p, TK_THEN);

    jmpToElse = EmitFwdJump(p, OP_JMPF);
    StackPop(p, 1);

    ParseBlock(p);
  }

  if (Check(p, TK_ELSE)) {
    PushPatch(p, EmitFwdJump(p, OP_JMP)); /* Arm done -> jump to end */

    PatchFwdJump(p, jmpToElse, MLuaCodePos(fs));
    haveElse = TRUE;

    Advance(p); /* Skip 'else' */
    ParseBlock(p);
  }

  Expect(p, TK_END);

  if (!haveElse) {
    PatchFwdJump(p, jmpToElse, MLuaCodePos(fs));
  }
  PatchAll(p, endWatermark, MLuaCodePos(fs));
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
  Expect(p, TK_DO);

  jmpToEnd = EmitFwdJump(p, OP_JMPF);
  StackPop(p, 1);

  bodyBase = fs->NumLocals; /* Body locals start here */
  ParseBlock(p);
  Expect(p, TK_END);

  /* End-of-iteration: close captures of body locals, then jump back */
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

  ParseBlock(p);
  Expect(p, TK_UNTIL);

  /* The until-condition can still see (and capture) body locals, so the
   * per-iteration close goes after the condition but before the jump. */
  ParseExpr(p);
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

    Expect(p, TK_COMMA);

    /* Push limit onto stack */
    ParseExpr(p);

    /* Push step onto stack (default 1) */
    if (Match(p, TK_COMMA)) {
      ParseExpr(p);
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
    ParseBlock(p);
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
    /* We expect 1-3 expressions (or a single call that returns 3) */
    Size savedPos = MLuaCodePos(fs);
    ParseExpr(p);

    /* Check if the expression was a function call */
    int iterExprIsCall = 0;
    if (MLuaCodePos(fs) > savedPos) {
      Size lastPos = MLuaCodePos(fs) - 1;
      if (lastPos >= 1) {
        U8 lastOp = fs->Proto->Code[lastPos - 1];
        iterExprIsCall = (lastOp == OP_CALL);
      }
    }

    /* Store iterator results (f, s, v) to shadow locals */
    if (iterExprIsCall) {
      /* Function call (like pairs(t)) returns 3 values, store in reverse order
       */
      /* Stack has: [f, s, v] with v on top */
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
    ParseBlock(p);
    Expect(p, TK_END);

    /* End-of-iteration: close captures of loop vars and body locals */
    EmitCloseIfCaptured(p, (U8)(iterSlot + 5));

    /* Jump back to loop head (the body-target store) */
    EmitBackJump(p, OP_JMP, loopHead);

    /* Exit point - nil-fallthrough jump and breaks land here */
    Size exitPos = MLuaCodePos(fs);
    EmitCloseIfCaptured(p, (U8)(iterSlot + 5));

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
    MLuaEmitOp(fs, OP_SETTABLE);
    MLuaEmitOpB(fs, OP_POP, 1);
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

  proto->MaxStackSize = (U8)(fs.MaxStack > 2 ? fs.MaxStack : 2);
  proto->NumLocals =
      fs.MaxLocals; /* Reserve the PEAK slot count (loops release slots) */

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
  PushPatch(p, EmitFwdJump(p, OP_JMP));
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

  if (Check(p, TK_ASSIGN)) {
    /* Assignment */
    if (accessInfo.accessType == 1) {
      /* t[k] = v => Stack has: [t, k], consume =, parse value, emit SETTABLE.
       * SETTABLE leaves t on the stack; drop it. */
      Advance(p);
      ParseExpr(p);
      MLuaEmitOp(fs, OP_SETTABLE);
      MLuaEmitOpB(fs, OP_POP, 1);
      StackPop(p, 3); /* Pop t, k, v */
    } else if (accessInfo.accessType == 2) {
      /* t.field = v => Stack has: [t], consume =, parse value, emit SETTABLE.
       * SETTABLE leaves t on the stack; drop it. */
      Advance(p);
      ParseExpr(p);
      MLuaEmitOpB(fs, OP_LOADK, (U8)accessInfo.fieldConstIdx);
      MLuaEmitOp(fs, OP_SWAP);
      MLuaEmitOp(fs, OP_SETTABLE);
      MLuaEmitOpB(fs, OP_POP, 1);
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
    if (accessInfo.accessType == 1) {
      MLuaEmitOp(fs, OP_GETTABLE);
      StackPop(p, 1);
    } else if (accessInfo.accessType == 2) {
      MLuaEmitOpB(fs, OP_LOADK, (U8)accessInfo.fieldConstIdx);
      MLuaEmitOp(fs, OP_GETTABLE);
    }
    /* Pop result */
    MLuaEmitOpB(fs, OP_POP, 1);
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
    ParseBlock(p);
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
  while (!Check(p, TK_END) && !Check(p, TK_ELSE) && !Check(p, TK_ELSEIF) &&
         !Check(p, TK_UNTIL) && !Check(p, TK_EOF)) {
    ParseStatement(p);
    Match(p, TK_SEMICOLON);
  }
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

  proto->MaxStackSize = (U8)(fs.MaxStack > 2 ? fs.MaxStack : 2);
  proto->NumLocals =
      fs.MaxLocals; /* Reserve the PEAK slot count (loops release slots) */

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
