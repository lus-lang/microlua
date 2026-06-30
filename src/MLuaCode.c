/*
 * MicroLua - MLuaCode.c
 * Bytecode generation for stack-based VM
 */

#include "MLuaCode.h"
#include "MLuaString.h"

/* ========================================================================== */
/* Instruction Sizes                                                          */
/* ========================================================================== */

Size MLuaOpSize(MLuaOpCode op) {
  switch (op) {
  /* 1-byte instructions (no operand) */
  case OP_NOP:
  case OP_LOADNIL:
  case OP_LOADTRUE:
  case OP_LOADFALSE:
  case OP_LOADK_S:
  case OP_GETLOCAL_S:
  case OP_SETLOCAL_S:
  case OP_GETGLOBAL:
  case OP_SETGLOBAL:
  case OP_DUP:
  case OP_SWAP:
  case OP_NEWTABLE:
  case OP_GETTABLE:
  case OP_SETTABLE:
  case OP_APPEND:
  case OP_NOT:
  case OP_EQ:
  case OP_LT:
  case OP_LE:
  case OP_NEQ:
  case OP_ADD:
  case OP_SUB:
  case OP_MUL:
  case OP_DIV:
  case OP_IDIV:
  case OP_MOD:
  case OP_POW:
  case OP_UNM:
  case OP_LEN:
  case OP_BAND:
  case OP_BOR:
  case OP_BXOR:
  case OP_SHL:
  case OP_SHR:
  case OP_BNOT:
  case OP_JMP_S:
  case OP_LOOP_S:
  case OP_CLOSURE_S:
  case OP_RET0:
  case OP_RET1:
    return 1;

  /* 2-byte instructions (opcode + 8-bit operand) */
  case OP_LOADINT:
  case OP_LOADK:
  case OP_GETLOCAL:
  case OP_SETLOCAL:
  case OP_GETARG:
  case OP_SETARG:
  case OP_GETUPVAL:
  case OP_SETUPVAL:
  case OP_POP:
  case OP_JMP:
  case OP_JMPF:
  case OP_JMPT:
  case OP_LOOP:
  case OP_NLOOP_PREP:
  case OP_NLOOP_STEP:
  case OP_GLOOP_SETUP:
  case OP_GLOOP_CALL:
  case OP_GLOOP_STEP:
  case OP_CLOSURE:
  case OP_CALL:
  case OP_RET:
  case OP_VARARG:
  case OP_TAILCALL:
  case OP_CONCAT:
    return 2;

  default:
    return 1;
  }
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

  newCap = (p->CodeCap == 0) ? 64 : p->CodeCap * 2;
  newCode = (U8 *)MLuaAlloc(fs->L, newCap);

  if (!newCode) {
    return FALSE;
  }

  if (p->Code) {
    MemCpy(newCode, p->Code, p->CodeSize);
  }

  p->Code = newCode;
  p->CodeCap = newCap;
  return TRUE;
}

Size MLuaEmitBytes(MLuaFuncState *fs, const U8 *bytes, Size count) {
  MLuaProto *p = fs->Proto;
  Size i;

  while (p->CodeSize + count > p->CodeCap) {
    if (!GrowCode(fs)) {
      return 0; /* Error */
    }
  }

  for (i = 0; i < count; i++) {
    p->Code[p->CodeSize + i] = bytes[i];
  }

  Size pos = p->CodeSize;
  p->CodeSize += count;
  return pos;
}

Size MLuaEmitOp(MLuaFuncState *fs, MLuaOpCode op) {
  U8 b = (U8)op;
  return MLuaEmitBytes(fs, &b, 1);
}

Size MLuaEmitOpB(MLuaFuncState *fs, MLuaOpCode op, U8 b) {
  U8 bytes[2];
  bytes[0] = (U8)op;
  bytes[1] = b;
  return MLuaEmitBytes(fs, bytes, 2);
}

Size MLuaEmitOpK(MLuaFuncState *fs, MLuaOpCode op, U16 k) {
  U8 bytes[3];
  bytes[0] = (U8)op;
  bytes[1] = (U8)(k & 0xFF);        /* Low byte */
  bytes[2] = (U8)((k >> 8) & 0xFF); /* High byte */
  return MLuaEmitBytes(fs, bytes, 3);
}

Size MLuaCodePos(MLuaFuncState *fs) { return fs->Proto->CodeSize; }

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

static Bool GrowConstants(MLuaFuncState *fs) {
  MLuaProto *p = fs->Proto;
  Size newCap;
  MLuaValue *newK;

  newCap = (p->ConstantsCap == 0) ? 16 : p->ConstantsCap * 2;
  newK = (MLuaValue *)MLuaAlloc(fs->L, newCap * sizeof(MLuaValue));

  if (!newK) {
    return FALSE;
  }

  if (p->Constants) {
    MemCpy(newK, p->Constants, p->ConstantsSize * sizeof(MLuaValue));
  }

  p->Constants = newK;
  p->ConstantsCap = newCap;
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
  if (p->ConstantsSize >= p->ConstantsCap) {
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

  /* Store as signed 8-bit */
  p->Code[jmp + 1] = (U8)(I8)offset;
}

/* ========================================================================== */
/* Opcode Names                                                               */
/* ========================================================================== */

const char *MLuaOpName(MLuaOpCode op) {
  static const char *names[] = {
      [OP_NOP] = "NOP",
      [OP_LOADNIL] = "LOADNIL",
      [OP_LOADTRUE] = "LOADTRUE",
      [OP_LOADFALSE] = "LOADFALSE",
      [OP_LOADINT] = "LOADINT",
      [OP_LOADK] = "LOADK",
      [OP_LOADK_S] = "LOADK_S",
      [OP_GETLOCAL] = "GETLOCAL",
      [OP_SETLOCAL] = "SETLOCAL",
      [OP_GETLOCAL_S] = "GETLOCAL_S",
      [OP_SETLOCAL_S] = "SETLOCAL_S",
      [OP_GETARG] = "GETARG",
      [OP_SETARG] = "SETARG",
      [OP_GETUPVAL] = "GETUPVAL",
      [OP_SETUPVAL] = "SETUPVAL",
      [OP_GETGLOBAL] = "GETGLOBAL",
      [OP_SETGLOBAL] = "SETGLOBAL",
      [OP_POP] = "POP",
      [OP_DUP] = "DUP",
      [OP_SWAP] = "SWAP",
      [OP_NEWTABLE] = "NEWTABLE",
      [OP_GETTABLE] = "GETTABLE",
      [OP_SETTABLE] = "SETTABLE",
      [OP_APPEND] = "APPEND",
      [OP_NOT] = "NOT",
      [OP_EQ] = "EQ",
      [OP_LT] = "LT",
      [OP_LE] = "LE",
      [OP_NEQ] = "NEQ",
      [OP_ADD] = "ADD",
      [OP_SUB] = "SUB",
      [OP_MUL] = "MUL",
      [OP_DIV] = "DIV",
      [OP_IDIV] = "IDIV",
      [OP_MOD] = "MOD",
      [OP_POW] = "POW",
      [OP_UNM] = "UNM",
      [OP_LEN] = "LEN",
      [OP_BAND] = "BAND",
      [OP_BOR] = "BOR",
      [OP_BXOR] = "BXOR",
      [OP_SHL] = "SHL",
      [OP_SHR] = "SHR",
      [OP_BNOT] = "BNOT",
      [OP_JMP] = "JMP",
      [OP_JMPF] = "JMPF",
      [OP_JMPT] = "JMPT",
      [OP_LOOP] = "LOOP",
      [OP_JMP_S] = "JMP_S",
      [OP_LOOP_S] = "LOOP_S",
      [OP_NLOOP_PREP] = "NLOOP_PREP",
      [OP_NLOOP_STEP] = "NLOOP_STEP",
      [OP_GLOOP_SETUP] = "GLOOP_SETUP",
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

/* ========================================================================== */
/* Exp-Golomb Line Number Compression                                         */
/* ========================================================================== */

/*
 * Exp-Golomb encoding (order 0):
 * - For a non-negative integer x, the encoding is:
 *   1. Compute n = floor(log2(x + 1))
 *   2. Write n zeros, followed by the (n+1)-bit binary representation of x+1
 *
 * The encoding is self-delimiting: read zeros until a 1 is encountered,
 * count gives n, then read n more bits to get the full (n+1)-bit suffix.
 *
 * Examples:
 *   0 -> 1           (1 bit)
 *   1 -> 010         (3 bits)
 *   2 -> 011         (3 bits)
 *   3 -> 00100       (5 bits)
 *   4 -> 00101       (5 bits)
 *   5 -> 00110       (5 bits)
 *   6 -> 00111       (5 bits)
 *   7 -> 0001000     (7 bits)
 *
 * Signed Exp-Golomb (interleaved):
 *   0 -> encode(0), 1 -> encode(1), -1 -> encode(2),
 *   2 -> encode(3), -2 -> encode(4), etc.
 *   Formula: d >= 0: encode(2*d), d < 0: encode(2*|d| - 1)
 */

/* Bit writer state - tracks bit position within byte stream */
typedef struct {
  MLuaProto *proto;
  MLuaState *L;
  int bitOffset; /* Current bit offset within LineInfo (0-based) */
} BitWriter;

/* Bit reader state */
typedef struct {
  const U8 *buf;
  Size len;
  int bitOffset; /* Current bit position */
} BitReader;

/* Grow line info buffer */
static Bool GrowLineInfo(MLuaProto *p, MLuaState *L) {
  Size newCap;
  U8 *newBuf;

  newCap = (p->LineInfoCap == 0) ? 32 : p->LineInfoCap * 2;
  newBuf = (U8 *)MLuaAlloc(L, newCap);

  if (!newBuf) {
    return FALSE;
  }

  if (p->LineInfo) {
    MemCpy(newBuf, p->LineInfo, p->LineInfoSize);
  }

  p->LineInfo = newBuf;
  p->LineInfoCap = newCap;
  return TRUE;
}

/* Write a single bit to the bit stream */
static void WriteBit(BitWriter *w, int bit) {
  Size byteIdx = (Size)(w->bitOffset / 8);
  int bitIdx = w->bitOffset % 8;

  /* Ensure capacity */
  while (byteIdx >= w->proto->LineInfoCap) {
    if (!GrowLineInfo(w->proto, w->L)) {
      return;
    }
  }

  /* Extend size if needed */
  if (byteIdx >= w->proto->LineInfoSize) {
    w->proto->LineInfoSize = byteIdx + 1;
    w->proto->LineInfo[byteIdx] = 0; /* Initialize new byte to 0 */
  }

  /* Write bit (MSB first within each byte) */
  if (bit) {
    w->proto->LineInfo[byteIdx] |= (U8)(0x80 >> bitIdx);
  }

  w->bitOffset++;
}

/* Read a single bit from the bit stream */
static int ReadBit(BitReader *r) {
  Size byteIdx = (Size)(r->bitOffset / 8);
  int bitIdx = r->bitOffset % 8;

  if (byteIdx >= r->len) {
    return 0; /* Past end of buffer */
  }

  r->bitOffset++;

  /* Read bit (MSB first within each byte) */
  return (r->buf[byteIdx] >> (7 - bitIdx)) & 1;
}

/* Count bits needed to represent value (floor(log2(x)) + 1) */
static int CountBits(Size x) {
  int n = 0;
  while (x > 0) {
    n++;
    x >>= 1;
  }
  return n;
}

/*
 * Emit Exp-Golomb encoded unsigned value.
 * Writes n zeros followed by (n+1) bits of (value+1), where n =
 * floor(log2(value+1)).
 */
static void EmitExpGolomb(BitWriter *w, Size value) {
  Size code = value + 1;       /* The value we encode in binary */
  int n = CountBits(code) - 1; /* Number of leading zeros */
  int i;

  /* Write n zeros */
  for (i = 0; i < n; i++) {
    WriteBit(w, 0);
  }

  /* Write code in (n+1) bits, MSB first */
  for (i = n; i >= 0; i--) {
    WriteBit(w, (int)((code >> i) & 1));
  }
}

/*
 * Decode Exp-Golomb value from bit stream.
 * Reads zeros until a 1 is encountered, then reads that many more bits.
 */
static Size DecodeExpGolomb(BitReader *r) {
  int n = 0;
  Size code;
  int i;

  /* Count leading zeros */
  while (ReadBit(r) == 0) {
    n++;
    /* Safety limit to prevent infinite loop on corrupted data */
    if (n > 64) {
      return 0;
    }
  }

  /* We've read the leading 1 bit, now read n more bits */
  code = 1;
  for (i = 0; i < n; i++) {
    code = (code << 1) | (Size)ReadBit(r);
  }

  return code - 1; /* Decoded value is code - 1 */
}

/* Emit line number delta (signed) */
void MLuaEmitLineDelta(MLuaFuncState *fs, int delta) {
  MLuaProto *p = fs->Proto;
  Size encoded;
  BitWriter w;

  /* Convert signed to unsigned using signed Exp-Golomb mapping */
  if (delta >= 0) {
    encoded = (Size)(2 * delta);
  } else {
    encoded = (Size)(2 * (-delta) - 1);
  }

  /* Initialize bit writer at current end of line info */
  w.proto = p;
  w.L = fs->L;
  w.bitOffset = (int)(p->LineInfoSize * 8);

  /* For first write, we need to track partial bytes properly */
  /* Store bit count in a separate field, but for now we round up to bytes */

  EmitExpGolomb(&w, encoded);

  /* Update size to include any partial final byte */
  p->LineInfoSize = (Size)((w.bitOffset + 7) / 8);
}

/* Track current line and add to LineMap */
void MLuaEmitLine(MLuaFuncState *fs, Size line) {
  MLuaProto *p = fs->Proto;
  Size currentPC = p->CodeSize; /* PC where next bytecode will be emitted */

  /* Set LineDefined on first call (function's starting line) */
  if (p->LineDefined == 0 && line > 0) {
    p->LineDefined = line;
  }

  /* Skip if same line as last entry */
  if (line == fs->LastLine && line > 0) {
    return;
  }
  fs->LastLine = line;

  /* Grow LineMap if needed */
  if (p->LineMapSize >= p->LineMapCap) {
    Size newCap = (p->LineMapCap == 0) ? 8 : p->LineMapCap * 2;
    Size newBytes = newCap * sizeof(p->LineMap[0]);
    void *newMap = MLuaAlloc(fs->L, newBytes);
    if (!newMap) {
      return; /* Allocation failed, skip line info */
    }
    if (p->LineMap) {
      MemCpy(newMap, p->LineMap, p->LineMapSize * sizeof(p->LineMap[0]));
    }
    p->LineMap = newMap;
    p->LineMapCap = newCap;
  }

  /* Add entry */
  p->LineMap[p->LineMapSize].PC = currentPC;
  p->LineMap[p->LineMapSize].Line = line;
  p->LineMapSize++;
}

/* Get line number for a given PC offset using LineMap */
Size MLuaGetLine(MLuaProto *p, Size pc) {
  Size line = p->LineDefined;
  Size i;

  /* Use LineMap if available */
  if (p->LineMap && p->LineMapSize > 0) {
    /* Linear search for last entry with PC <= queried pc */
    for (i = 0; i < p->LineMapSize; i++) {
      if (p->LineMap[i].PC <= pc) {
        line = p->LineMap[i].Line;
      } else {
        break; /* Entries are sorted by PC, so we can stop */
      }
    }
  }

  return line;
}
