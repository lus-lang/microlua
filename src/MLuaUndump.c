/*
 * MicroLua - MLuaUndump.c
 * Portable bytecode deserialization.
 */

#include "MLuaUndump.h"
#include "MLuaDump.h"
#include "MLuaString.h"
#if MLUA_FLOAT_BITS < 64
#include "MLuaFloatBits.h" /* boundary conversion; not needed for binary64 */
#endif

#define BC_FORMAT_OFFICIAL 0
#define BC_FLAGS_NONE 0
#define BC_INT_SIZE 4
#define BC_SIZE_FIELD_SIZE 4
#define BC_INSTRUCTION_SIZE 1
#define BC_NUMBER_SIZE 8

#define BC_MAX_CODE_SIZE ((U32)0x01000000)
#define BC_MAX_COUNT ((U32)0x00100000)

typedef struct {
  MLuaState *L;
  const U8 *Data;
  Size Len;
  Size Pos;
  int Endian;
  const char *Error;
} BCReader;

static void Fail(BCReader *r, const char *msg) {
  if (!r->Error) {
    r->Error = msg;
  }
}

static Bool Need(BCReader *r, Size n) {
  if (r->Error) {
    return FALSE;
  }
  if (n > r->Len || r->Pos > r->Len - n) {
    Fail(r, "truncated bytecode");
    return FALSE;
  }
  return TRUE;
}

static U8 ReadU8(BCReader *r) {
  if (!Need(r, 1)) {
    return 0;
  }
  return r->Data[r->Pos++];
}

static U32 ReadU32(BCReader *r) {
  U32 b0, b1, b2, b3;
  if (!Need(r, 4)) {
    return 0;
  }
  b0 = r->Data[r->Pos++];
  b1 = r->Data[r->Pos++];
  b2 = r->Data[r->Pos++];
  b3 = r->Data[r->Pos++];
  if (r->Endian == MLUA_BYTECODE_ENDIAN_BIG) {
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
  }
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static I32 ReadI32(BCReader *r) { return (I32)ReadU32(r); }

static U64 ReadU64(BCReader *r) {
  U64 value = 0;
  int i;
  if (!Need(r, 8)) {
    return 0;
  }
  if (r->Endian == MLUA_BYTECODE_ENDIAN_BIG) {
    for (i = 0; i < 8; i++) {
      value = (value << 8) | (U64)r->Data[r->Pos++];
    }
  } else {
    for (i = 0; i < 8; i++) {
      value |= ((U64)r->Data[r->Pos++]) << (i * 8);
    }
  }
  return value;
}

static double ReadNumber(BCReader *r) {
#if MLUA_FLOAT_BITS < 64
  /* Narrow canonical binary64 from the wire to the native (narrow) float. */
  return (double)mlua_bits64_to_f(ReadU64(r));
#else
  union {
    double d;
    U64 u;
  } conv;
  conv.u = ReadU64(r);
  return conv.d;
#endif
}

static Bool ReadBytes(BCReader *r, U8 *out, Size len) {
  if (!Need(r, len)) {
    return FALSE;
  }
  MemCpy(out, r->Data + r->Pos, len);
  r->Pos += len;
  return TRUE;
}

static MLuaValue ReadValue(BCReader *r) {
  U8 tag = ReadU8(r);
  U32 len;
  MLuaValue v;

  switch (tag) {
  case 0:
    return MLUA_NIL;
  case 1:
    return MLUA_FALSE;
  case 2:
    return MLUA_TRUE;
  case 3:
    return MLuaMakeInt(r->L, ReadI32(r));
  case 4:
    return MLuaMakeNumber(r->L, ReadNumber(r));
  case 5:
    len = ReadU32(r);
    if (len > BC_MAX_CODE_SIZE) {
      Fail(r, "bytecode string too large");
      return MLUA_NIL;
    }
    if (!Need(r, (Size)len)) {
      return MLUA_NIL;
    }
    v = MLuaStringNew(r->L, (const char *)(r->Data + r->Pos), (Size)len);
    r->Pos += (Size)len;
    return v;
  default:
    Fail(r, "invalid bytecode constant tag");
    return MLUA_NIL;
  }
}

static Bool AllocArray(BCReader *r, void **out, U32 count, Size elemSize,
                       const char *what) {
  Size bytes;
  *out = NULL;
  if (count == 0) {
    return TRUE;
  }
  if (count > BC_MAX_COUNT || (Size)count > MLUA_IDX_MAX ||
      (elemSize != 0 && (Size)count > ((Size)-1 / elemSize))) {
    Fail(r, what);
    return FALSE;
  }
  bytes = (Size)count * elemSize;
  *out = MLuaAlloc(r->L, bytes);
  if (!*out) {
    Fail(r, "out of memory loading bytecode");
    return FALSE;
  }
  MemSet(*out, 0, bytes);
  return TRUE;
}

static Bool ValidateCode(BCReader *r, MLuaProto *p) {
  Size pc = 0;
  while (pc < p->CodeSize) {
    U8 op = p->Code[pc];
    Size size;
    U8 operand = 0;

    if (op >= OP_COUNT) {
      Fail(r, "invalid bytecode opcode");
      return FALSE;
    }

    size = MLuaOpSize((MLuaOpCode)op);
    if (size != 1 && size != 2) {
      Fail(r, "invalid bytecode instruction size");
      return FALSE;
    }
    if (pc + size > p->CodeSize) {
      Fail(r, "truncated bytecode instruction");
      return FALSE;
    }
    if (size == 2) {
      operand = p->Code[pc + 1];
    }

    switch ((MLuaOpCode)op) {
    case OP_CLEARLOCAL:
    case OP_GETLOCAL:
    case OP_GETLOCAL_CLEAR:
    case OP_SETLOCAL:
      if (operand >= p->NumLocals) {
        Fail(r, "bytecode local index out of range");
        return FALSE;
      }
      break;
    case OP_LOADK:
    case OP_GETGLOBAL_K:
      if ((Size)operand >= p->ConstantsSize) {
        Fail(r, "bytecode constant index out of range");
        return FALSE;
      }
      break;
    case OP_GETTABLE_LL:
    case OP_SETTABLE_LL:
      /* Both packed nibbles are local slots */
      if ((operand >> 4) >= p->NumLocals || (operand & 0x0F) >= p->NumLocals) {
        Fail(r, "bytecode local index out of range");
        return FALSE;
      }
      break;
    case OP_CLOSURE:
      if ((Size)operand >= p->ProtosSize) {
        Fail(r, "bytecode proto index out of range");
        return FALSE;
      }
      break;
    case OP_GETUPVAL:
    case OP_SETUPVAL:
      if ((Size)operand >= p->UpvaluesSize) {
        Fail(r, "bytecode upvalue index out of range");
        return FALSE;
      }
      break;
    default:
      break;
    }

    pc += size;
  }
  return TRUE;
}

static MLuaProto *ReadProto(BCReader *r) {
  MLuaProto *p = MLuaProtoNew(r->L);
  U32 count;
  U32 i;

  if (!p) {
    Fail(r, "out of memory loading bytecode");
    return NULL;
  }

  p->Source = ReadValue(r);
  if (r->Error) {
    return NULL;
  }
  if (!IsNil(p->Source) && !IsAnyString(p->Source)) {
    Fail(r, "invalid bytecode source name");
    return NULL;
  }
  {
    U32 lineDefined = ReadU32(r);
    p->LineDefined = (MLUA_LINE_T)(lineDefined > (U32)MLUA_LINE_MAX
                                       ? (U32)MLUA_LINE_MAX
                                       : lineDefined);
  }

  p->NumParams = ReadU8(r);
  p->NumLocals = ReadU8(r);
  p->IsVararg = ReadU8(r);
  p->MaxStackSize = ReadU8(r);
  if (p->MaxStackSize == 0 || p->IsVararg > 1 ||
      p->NumLocals < p->NumParams) {
    Fail(r, "invalid bytecode stack size");
    return NULL;
  }

  count = ReadU32(r);
  if (count > BC_MAX_CODE_SIZE) {
    Fail(r, "bytecode code section too large");
    return NULL;
  }
  p->CodeSize = (Size)count;
  if (!AllocArray(r, (void **)&p->Code, count, sizeof(U8),
                  "bytecode code section too large") ||
      !ReadBytes(r, p->Code, p->CodeSize)) {
    return NULL;
  }

  count = ReadU32(r);
  p->ConstantsSize = (Size)count;
  if (!AllocArray(r, (void **)&p->Constants, count, sizeof(MLuaValue),
                  "bytecode constant table too large")) {
    return NULL;
  }
  for (i = 0; i < count; i++) {
    p->Constants[i] = ReadValue(r);
    if (r->Error) {
      return NULL;
    }
  }

  count = ReadU32(r);
  p->UpvaluesSize = (Size)count;
  if (!AllocArray(r, (void **)&p->Upvalues, count, sizeof(MLuaUpvalDesc),
                  "bytecode upvalue table too large")) {
    return NULL;
  }
  for (i = 0; i < count; i++) {
    p->Upvalues[i].InStack = ReadU8(r);
    p->Upvalues[i].Index = ReadU8(r);
    if (p->Upvalues[i].InStack > 1) {
      Fail(r, "invalid bytecode upvalue descriptor");
      return NULL;
    }
  }

  count = ReadU32(r);
  p->ProtosSize = (Size)count;
  if (!AllocArray(r, (void **)&p->Protos, count, sizeof(MLuaProto *),
                  "bytecode proto table too large")) {
    return NULL;
  }
  for (i = 0; i < count; i++) {
    p->Protos[i] = ReadProto(r);
    if (!p->Protos[i]) {
      return NULL;
    }
  }

  count = ReadU32(r);
#if MLUA_ENABLE_LINEINFO
  {
    /* Keep the largest prefix whose PCs/lines fit MLUA_LINE_T; the rest of
     * the section is consumed but dropped, mirroring emit-side saturation. */
    U32 prevPC = 0;
    Size kept = 0;
    Bool keepMore = TRUE;
    if (!AllocArray(r, (void **)&p->LineMap, count, sizeof(p->LineMap[0]),
                    "bytecode line map too large")) {
      return NULL;
    }
    for (i = 0; i < count; i++) {
      U32 pcVal = ReadU32(r);
      U32 lineVal = ReadU32(r);
      if (i > 0 && pcVal < prevPC) {
        Fail(r, "bytecode line map out of order");
        return NULL;
      }
      prevPC = pcVal;
      if (pcVal > (U32)MLUA_LINE_MAX || lineVal > (U32)MLUA_LINE_MAX) {
        keepMore = FALSE;
      }
      if (keepMore) {
        p->LineMap[kept].PC = (MLUA_LINE_T)pcVal;
        p->LineMap[kept].Line = (MLUA_LINE_T)lineVal;
        kept++;
      }
    }
    p->LineMapSize = kept;
  }
#else
  /* Line info disabled: consume the section without storing it. */
  for (i = 0; i < count; i++) {
    (void)ReadU32(r);
    (void)ReadU32(r);
  }
#endif

  if (!ValidateCode(r, p)) {
    return NULL;
  }

  return p;
}

MLuaProto *MLuaUndump(MLuaState *L, const char *data, Size len) {
  BCReader r;
  MLuaProto *proto;
  U8 version, format, flags, intSize, sizeFieldSize, instructionSize;
  U8 numberSize, floatFormat;

  MemSet(&r, 0, sizeof(r));
  r.L = L;
  r.Data = (const U8 *)data;
  r.Len = len;

  if (!Need(&r, 13)) {
    L->ErrorMsg = r.Error;
    return NULL;
  }

  if (ReadU8(&r) != 0x1B || ReadU8(&r) != 'M' || ReadU8(&r) != 'L' ||
      ReadU8(&r) != 'u') {
    L->ErrorMsg = "bad bytecode magic";
    return NULL;
  }

  version = ReadU8(&r);
  format = ReadU8(&r);
  r.Endian = ReadU8(&r);
  flags = ReadU8(&r);
  intSize = ReadU8(&r);
  sizeFieldSize = ReadU8(&r);
  instructionSize = ReadU8(&r);
  numberSize = ReadU8(&r);
  floatFormat = ReadU8(&r);

  if (version != MLUA_BYTECODE_VERSION) {
    L->ErrorMsg = "unsupported bytecode version";
    return NULL;
  }
  if (format != BC_FORMAT_OFFICIAL || flags != BC_FLAGS_NONE) {
    L->ErrorMsg = "unsupported bytecode format";
    return NULL;
  }
  if (r.Endian != MLUA_BYTECODE_ENDIAN_LITTLE &&
      r.Endian != MLUA_BYTECODE_ENDIAN_BIG) {
    L->ErrorMsg = "unsupported bytecode endianness";
    return NULL;
  }
  if (intSize != BC_INT_SIZE || sizeFieldSize != BC_SIZE_FIELD_SIZE ||
      instructionSize != BC_INSTRUCTION_SIZE || numberSize != BC_NUMBER_SIZE ||
      floatFormat != MLUA_BYTECODE_FLOAT_IEEE754) {
    L->ErrorMsg = "unsupported bytecode target format";
    return NULL;
  }

  proto = ReadProto(&r);
  if (!proto) {
    L->ErrorMsg = r.Error ? r.Error : "invalid bytecode";
    return NULL;
  }
  if (r.Pos != r.Len) {
    L->ErrorMsg = "trailing data after bytecode chunk";
    return NULL;
  }
  return proto;
}
