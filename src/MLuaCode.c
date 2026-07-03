/*
 * MicroLua - MLuaCode.c
 * Bytecode generation for stack-based VM
 */

#include "MLuaCode.h"
#include "MLuaString.h"

/* ========================================================================== */
/* Instruction Sizes                                                          */
/* ========================================================================== */

/*
 * Size 0 marks a byte that is not a live opcode: the bytecode loader rejects
 * the chunk and the parser's code walks bail conservatively. A flat table is
 * markedly smaller than the equivalent three-way switch on every target.
 */
static const U8 OpSizes[OP_COUNT] = {
    /* 1-byte instructions (no operand) */
    [OP_NOP] = 1,
    [OP_LOADNIL] = 1,
    [OP_LOADTRUE] = 1,
    [OP_LOADFALSE] = 1,
    [OP_GETGLOBAL] = 1,
    [OP_SETGLOBAL] = 1,
    [OP_DUP] = 1,
    [OP_SWAP] = 1,
    [OP_NEWTABLE] = 1,
    [OP_GETTABLE] = 1,
    [OP_SETTABLE] = 1,
    [OP_APPEND] = 1,
    [OP_APPENDM] = 1,
    [OP_SETTABLE_POP] = 1,
    [OP_NOT] = 1,
    [OP_EQ] = 1,
    [OP_LT] = 1,
    [OP_LE] = 1,
    [OP_NEQ] = 1,
    [OP_ADD] = 1,
    [OP_SUB] = 1,
    [OP_MUL] = 1,
    [OP_DIV] = 1,
    [OP_MOD] = 1,
    [OP_POW] = 1,
    [OP_UNM] = 1,
    [OP_LEN] = 1,
    [OP_JMP_S] = 1,
    [OP_LOOP_S] = 1,
    [OP_CLOSURE_S] = 1,
    [OP_RET0] = 1,
    [OP_RET1] = 1,

    /* 2-byte instructions (opcode + 8-bit operand) */
    [OP_LOADINT] = 2,
    [OP_LOADK] = 2,
    [OP_CLEARLOCAL] = 2,
    [OP_GETLOCAL_CLEAR] = 2,
    [OP_GETLOCAL] = 2,
    [OP_SETLOCAL] = 2,
    [OP_GETUPVAL] = 2,
    [OP_SETUPVAL] = 2,
    [OP_CLOSE] = 2,
    [OP_ADJUST] = 2,
    [OP_POP] = 2,
    [OP_JMP] = 2,
    [OP_JMPF] = 2,
    [OP_JMPT] = 2,
    [OP_LOOP] = 2,
    [OP_NLOOP_PREP] = 2,
    [OP_NLOOP_STEP] = 2,
    [OP_GLOOP_CALL] = 2,
    [OP_GLOOP_STEP] = 2,
    [OP_CLOSURE] = 2,
    [OP_CALL] = 2,
    [OP_CALLM] = 2,
    [OP_RET] = 2,
    [OP_VARARG] = 2,
    [OP_TAILCALL] = 2,
    [OP_CONCAT] = 2,
    [OP_GETGLOBAL_K] = 2,
    [OP_SETGLOBAL_K] = 2,
    [OP_GETFIELD_K] = 2,
    [OP_SETFIELD_K] = 2,
    [OP_SETFIELD_K_POP] = 2,
    [OP_GETTABLE_LL] = 2,
    [OP_SETTABLE_LL] = 2,
};

Size MLuaOpSize(MLuaOpCode op) {
  if ((unsigned)op >= (unsigned)OP_COUNT) {
    return 0;
  }
  return OpSizes[op];
}

/* ========================================================================== */
/* Prototype Allocation                                                       */
/* ========================================================================== */

MLuaProto *MLuaProtoNew(MLuaState *L) {
  MLuaGCHeader *gch;
  MLuaProto *p;

  gch = MLuaAllocObject(L, OBJTYPE_PROTO, sizeof(MLuaProto));
  if (!gch) {
    return NULL;
  }

  p = MLUA_PROTOHEADER(gch);
  MemSet(p, 0, sizeof(MLuaProto));

  return p;
}

/* ========================================================================== */
/* Code Emission                                                              */
/* ========================================================================== */

static Bool GrowCode(MLuaFuncState *fs) {
  MLuaProto *p = fs->Proto;
  Size newCap;
  U8 *newCode;

  if (fs->CodeCap >= MLUA_IDX_MAX) {
    fs->CodeOverflow = TRUE; /* body end reports "function too large" */
    return FALSE;
  }
  newCap = (fs->CodeCap == 0) ? 64 : (Size)fs->CodeCap * 2;
  if (newCap > MLUA_IDX_MAX) {
    newCap = MLUA_IDX_MAX;
  }
  newCode = (U8 *)MLuaAlloc(fs->L, newCap);

  if (!newCode) {
    return FALSE;
  }

  if (p->Code) {
    MemCpy(newCode, p->Code, p->CodeSize);
  }

  p->Code = newCode;
  fs->CodeCap = (MLuaIdx)newCap;
  return TRUE;
}

Size MLuaEmitBytes(MLuaFuncState *fs, const U8 *bytes, Size count) {
  MLuaProto *p = fs->Proto;
  Size i;

  while (p->CodeSize + count > fs->CodeCap) {
    if (!GrowCode(fs)) {
      return 0; /* Error */
    }
  }

  for (i = 0; i < count; i++) {
    p->Code[p->CodeSize + i] = bytes[i];
  }

  Size pos = p->CodeSize;
  p->CodeSize += count;
  fs->PrevInstrPos = fs->LastInstrPos;
  fs->LastInstrPos = pos;
  return pos;
}

Size MLuaEmitOp(MLuaFuncState *fs, MLuaOpCode op) {
  U8 b = (U8)op;
  return MLuaEmitBytes(fs, &b, 1);
}

Bool MLuaInsertBytes(MLuaFuncState *fs, Size pos, const U8 *bytes,
                     Size count) {
  MLuaProto *p = fs->Proto;
  Size i;

  if (pos > p->CodeSize) {
    return FALSE;
  }
  while (p->CodeSize + count > fs->CodeCap) {
    if (!GrowCode(fs)) {
      return FALSE;
    }
  }

  for (i = p->CodeSize; i > pos; i--) {
    p->Code[i + count - 1] = p->Code[i - 1];
  }
  for (i = 0; i < count; i++) {
    p->Code[pos + i] = bytes[i];
  }
  p->CodeSize += count;

  /* Positions recorded inside the shifted block move with it. (Callers
   * only insert where no jump target, patch position, or line-map entry
   * can point past `pos` -- see the fusion-repair call site.) */
  if (fs->LastCallEnd > pos) {
    fs->LastCallEnd += count;
  }
  if (fs->LastInstrPos != (Size)-1 && fs->LastInstrPos >= pos) {
    fs->LastInstrPos += count;
  }
  if (fs->PrevInstrPos != (Size)-1 && fs->PrevInstrPos >= pos) {
    fs->PrevInstrPos += count;
  }
  return TRUE;
}

Size MLuaEmitOpB(MLuaFuncState *fs, MLuaOpCode op, U8 b) {
  U8 bytes[2];
  bytes[0] = (U8)op;
  bytes[1] = b;
  return MLuaEmitBytes(fs, bytes, 2);
}

Size MLuaCodePos(MLuaFuncState *fs) { return fs->Proto->CodeSize; }

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

static Bool GrowConstants(MLuaFuncState *fs) {
  MLuaProto *p = fs->Proto;
  Size newCap;
  MLuaValue *newK;

  if (fs->ConstantsCap > MLUA_IDX_MAX / 2) {
    return FALSE; /* callers report "too many constants in function" */
  }
  newCap = (fs->ConstantsCap == 0) ? 16 : (Size)fs->ConstantsCap * 2;
  newK = (MLuaValue *)MLuaAlloc(fs->L, newCap * sizeof(MLuaValue));

  if (!newK) {
    return FALSE;
  }

  if (p->Constants) {
    MemCpy(newK, p->Constants, p->ConstantsSize * sizeof(MLuaValue));
  }

  p->Constants = newK;
  fs->ConstantsCap = (MLuaIdx)newCap;
  return TRUE;
}

int MLuaAddConstant(MLuaFuncState *fs, MLuaValue v) {
  MLuaProto *p = fs->Proto;
  Size i;

  /* Check if constant already exists */
  for (i = 0; i < p->ConstantsSize; i++) {
    if (MLuaRawEqual(p->Constants[i], v)) {
      return (int)i;
    }
  }

  /* Add new constant */
  if (p->ConstantsSize >= fs->ConstantsCap) {
    if (!GrowConstants(fs)) {
      return -1; /* Error */
    }
  }

  p->Constants[p->ConstantsSize] = v;
  return (int)p->ConstantsSize++;
}

int MLuaAddConstantRaw(MLuaFuncState *fs, MLuaValue v) {
  MLuaProto *p = fs->Proto;

  if (p->ConstantsSize >= fs->ConstantsCap) {
    if (!GrowConstants(fs)) {
      return -1; /* Error */
    }
  }

  p->Constants[p->ConstantsSize] = v;
  return (int)p->ConstantsSize++;
}

int MLuaAddStringK(MLuaFuncState *fs, const char *str, Size len) {
  MLuaValue s = MLuaStringNew(fs->L, str, len);
  if (IsNil(s)) {
    return -1;
  }
  return MLuaAddConstant(fs, s);
}

/* ========================================================================== */
/* Jump Patching                                                              */
/* ========================================================================== */

void MLuaPatchJump(MLuaFuncState *fs, Size jmp, Size target) {
  MLuaProto *p = fs->Proto;
  int offset;

  /* jmp points to the opcode, the 8-bit offset is at jmp+1 */
  if (jmp + 1 >= p->CodeSize) {
    return; /* Invalid jump position */
  }

  /* Calculate signed offset from instruction AFTER the jump (jmp + 2 for 2-byte
   * instr) */
  offset = (int)target - (int)(jmp + 2);

  /* Truncating silently would create a wild jump; flag it so the parser can
   * report "control structure too long" instead of emitting broken code. */
  if (offset < -128 || offset > 127) {
    fs->JumpOverflow = TRUE;
    return;
  }

  /* Store as signed 8-bit */
  p->Code[jmp + 1] = (U8)(I8)offset;
}

/* ========================================================================== */
/* Opcode Names                                                               */
/* ========================================================================== */

/* Only the MLUA_PROFILE_OPS dump consumes these strings; as an exported
 * symbol the table would otherwise survive linkers that run without
 * --gc-sections/LTO, so the guard makes its removal unconditional. */
#if MLUA_PROFILE_OPS
const char *MLuaOpName(MLuaOpCode op) {
  static const char *names[] = {
      [OP_NOP] = "NOP",
      [OP_LOADNIL] = "LOADNIL",
      [OP_LOADTRUE] = "LOADTRUE",
      [OP_LOADFALSE] = "LOADFALSE",
      [OP_CLEARLOCAL] = "CLEARLOCAL",
      [OP_GETLOCAL_CLEAR] = "GETLOCAL_CLEAR",
      [OP_LOADINT] = "LOADINT",
      [OP_LOADK] = "LOADK",
      [OP_GETLOCAL] = "GETLOCAL",
      [OP_SETLOCAL] = "SETLOCAL",
      [OP_GETUPVAL] = "GETUPVAL",
      [OP_SETUPVAL] = "SETUPVAL",
      [OP_GETGLOBAL] = "GETGLOBAL",
      [OP_SETGLOBAL] = "SETGLOBAL",
      [OP_POP] = "POP",
      [OP_DUP] = "DUP",
      [OP_SWAP] = "SWAP",
      [OP_CLOSE] = "CLOSE",
      [OP_ADJUST] = "ADJUST",
      [OP_APPENDM] = "APPENDM",
      [OP_CALLM] = "CALLM",
      [OP_NEWTABLE] = "NEWTABLE",
      [OP_GETTABLE] = "GETTABLE",
      [OP_SETTABLE] = "SETTABLE",
      [OP_APPEND] = "APPEND",
      [OP_GETGLOBAL_K] = "GETGLOBAL_K",
      [OP_GETTABLE_LL] = "GETTABLE_LL",
      [OP_SETTABLE_LL] = "SETTABLE_LL",
      [OP_SETTABLE_POP] = "SETTABLE_POP",
      [OP_NOT] = "NOT",
      [OP_EQ] = "EQ",
      [OP_LT] = "LT",
      [OP_LE] = "LE",
      [OP_NEQ] = "NEQ",
      [OP_ADD] = "ADD",
      [OP_SUB] = "SUB",
      [OP_MUL] = "MUL",
      [OP_DIV] = "DIV",
      [OP_MOD] = "MOD",
      [OP_POW] = "POW",
      [OP_UNM] = "UNM",
      [OP_LEN] = "LEN",
      [OP_JMP] = "JMP",
      [OP_JMPF] = "JMPF",
      [OP_JMPT] = "JMPT",
      [OP_LOOP] = "LOOP",
      [OP_JMP_S] = "JMP_S",
      [OP_LOOP_S] = "LOOP_S",
      [OP_NLOOP_PREP] = "NLOOP_PREP",
      [OP_NLOOP_STEP] = "NLOOP_STEP",
      [OP_GLOOP_CALL] = "GLOOP_CALL",
      [OP_GLOOP_STEP] = "GLOOP_STEP",
      [OP_CLOSURE] = "CLOSURE",
      [OP_CLOSURE_S] = "CLOSURE_S",
      [OP_CALL] = "CALL",
      [OP_RET] = "RET",
      [OP_RET0] = "RET0",
      [OP_RET1] = "RET1",
      [OP_VARARG] = "VARARG",
      [OP_TAILCALL] = "TAILCALL",
      [OP_CONCAT] = "CONCAT",
  };

  if (op >= 0 && op < OP_COUNT && names[op]) {
    return names[op];
  }
  return "UNKNOWN";
}
#endif /* MLUA_PROFILE_OPS */

/* ========================================================================== */
/* Line Number Info                                                           */
/* ========================================================================== */

/* Track current line and add to LineMap */
void MLuaEmitLine(MLuaFuncState *fs, Size line) {
  MLuaProto *p = fs->Proto;

  /* Set LineDefined on first call (function's starting line) */
  if (p->LineDefined == 0 && line > 0) {
    p->LineDefined =
        (MLUA_LINE_T)(line > MLUA_LINE_MAX ? MLUA_LINE_MAX : line);
  }

  /* Skip if same line as last entry */
  if (line == fs->LastLine && line > 0) {
    return;
  }
  fs->LastLine = line;

#if MLUA_ENABLE_LINEINFO
  {
    Size currentPC = p->CodeSize; /* PC where next bytecode is emitted */

    /* Saturate: once the PC or line outgrows MLUA_LINE_T the map keeps its
     * prefix and MLuaGetLine reports the last recorded line from there. */
    if (currentPC > MLUA_LINE_MAX || line > MLUA_LINE_MAX) {
      return;
    }

    /* Grow LineMap if needed */
    if (p->LineMapSize >= fs->LineMapCap) {
      Size newCap;
      Size newBytes;
      if (fs->LineMapCap > MLUA_IDX_MAX / 2) {
        return; /* map full; later lines degrade like an alloc failure */
      }
      newCap = (fs->LineMapCap == 0) ? 8 : (Size)fs->LineMapCap * 2;
      newBytes = newCap * sizeof(p->LineMap[0]);
      void *newMap = MLuaAlloc(fs->L, newBytes);
      if (!newMap) {
        return; /* Allocation failed, skip line info */
      }
      if (p->LineMap) {
        MemCpy(newMap, p->LineMap, p->LineMapSize * sizeof(p->LineMap[0]));
      }
      p->LineMap = newMap;
      fs->LineMapCap = (MLuaIdx)newCap;
    }

    /* Add entry */
    p->LineMap[p->LineMapSize].PC = (MLUA_LINE_T)currentPC;
    p->LineMap[p->LineMapSize].Line = (MLUA_LINE_T)line;
    p->LineMapSize++;
  }
#endif
}

/* Get line number for a given PC offset using LineMap */
Size MLuaGetLine(MLuaProto *p, Size pc) {
#if MLUA_ENABLE_LINEINFO
  Size line = p->LineDefined;
  Size i;

  /* Use LineMap if available */
  if (p->LineMap && p->LineMapSize > 0) {
    /* Linear search for last entry with PC <= queried pc */
    for (i = 0; i < p->LineMapSize; i++) {
      if ((Size)p->LineMap[i].PC <= pc) {
        line = p->LineMap[i].Line;
      } else {
        break; /* Entries are sorted by PC, so we can stop */
      }
    }
  }

  return line;
#else
  /* No line info in this build: 0 means "unknown" (traces print `?`). */
  UNUSED(p);
  UNUSED(pc);
  return 0;
#endif
}
