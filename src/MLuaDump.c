/*
 * MicroLua - MLuaDump.c
 * Portable bytecode serialization.
 */

#include "MLuaDump.h"
#include "MLuaFunc.h"
#include "MLuaString.h"

#define BC_FORMAT_OFFICIAL 0
#define BC_FLAGS_NONE 0
#define BC_INT_SIZE 4
#define BC_SIZE_FIELD_SIZE 4
#define BC_INSTRUCTION_SIZE 1
#define BC_NUMBER_SIZE 8

static Size DumpByte(char *buf, Size pos, Size cap, U8 b) {
  if (buf && pos < cap) {
    buf[pos] = (char)b;
  }
  return pos + 1;
}

static Size DumpBytes(char *buf, Size pos, Size cap, const U8 *data, Size len) {
  Size i;
  for (i = 0; i < len; i++) {
    pos = DumpByte(buf, pos, cap, data[i]);
  }
  return pos;
}

static Size DumpU32(char *buf, Size pos, Size cap, U32 val, int endian) {
  int i;
  for (i = 0; i < 4; i++) {
    int shift = (endian == MLUA_BYTECODE_ENDIAN_BIG) ? (24 - i * 8) : (i * 8);
    pos = DumpByte(buf, pos, cap, (U8)((val >> shift) & 0xFF));
  }
  return pos;
}

static Size DumpI32(char *buf, Size pos, Size cap, I32 val, int endian) {
  return DumpU32(buf, pos, cap, (U32)val, endian);
}

static Size DumpU64(char *buf, Size pos, Size cap, U64 val, int endian) {
  int i;
  for (i = 0; i < 8; i++) {
    int shift = (endian == MLUA_BYTECODE_ENDIAN_BIG) ? (56 - i * 8) : (i * 8);
    pos = DumpByte(buf, pos, cap, (U8)((val >> shift) & 0xFF));
  }
  return pos;
}

static Size DumpNumber(char *buf, Size pos, Size cap, double d, int endian) {
  union {
    double d;
    U64 u;
  } conv;
  conv.d = d;
  return DumpU64(buf, pos, cap, conv.u, endian);
}

static Bool FitsU32(Size value) { return value <= (Size)0xFFFFFFFFUL; }

static Size DumpProto(MLuaState *L, MLuaProto *proto, char *buf, Size pos,
                      Size cap, int endian);

static Size DumpValue(MLuaState *L, MLuaValue v, char *buf, Size pos, Size cap,
                      int endian) {
  UNUSED(L);
  if (IsNil(v)) {
    return DumpByte(buf, pos, cap, 0);
  }
  if (IsFalse(v)) {
    return DumpByte(buf, pos, cap, 1);
  }
  if (IsTrue(v)) {
    return DumpByte(buf, pos, cap, 2);
  }
  if (IsInt(v)) {
    pos = DumpByte(buf, pos, cap, 3);
    return DumpI32(buf, pos, cap, GetInt(v), endian);
  }
  if (MLuaIsNumber(v)) {
    pos = DumpByte(buf, pos, cap, 4);
    return DumpNumber(buf, pos, cap, MLuaToNumber(v), endian);
  }
  if (IsAnyString(v)) {
    const char *s = MLuaStringData(v);
    Size slen = MLuaStringLen(v);
    pos = DumpByte(buf, pos, cap, 5);
    pos = DumpU32(buf, pos, cap, (U32)slen, endian);
    return DumpBytes(buf, pos, cap, (const U8 *)s, slen);
  }
  return DumpByte(buf, pos, cap, 0);
}

static Size DumpProto(MLuaState *L, MLuaProto *proto, char *buf, Size pos,
                      Size cap, int endian) {
  Size i;

  pos = DumpValue(L, proto->Source, buf, pos, cap, endian);
  pos = DumpU32(buf, pos, cap, (U32)proto->LineDefined, endian);

  pos = DumpByte(buf, pos, cap, proto->NumParams);
  pos = DumpByte(buf, pos, cap, proto->NumLocals);
  pos = DumpByte(buf, pos, cap, proto->IsVararg);
  pos = DumpByte(buf, pos, cap, proto->MaxStackSize);

  pos = DumpU32(buf, pos, cap, (U32)proto->CodeSize, endian);
  pos = DumpBytes(buf, pos, cap, proto->Code, proto->CodeSize);

  pos = DumpU32(buf, pos, cap, (U32)proto->ConstantsSize, endian);
  for (i = 0; i < proto->ConstantsSize; i++) {
    pos = DumpValue(L, proto->Constants[i], buf, pos, cap, endian);
  }

  pos = DumpU32(buf, pos, cap, (U32)proto->UpvaluesSize, endian);
  for (i = 0; i < proto->UpvaluesSize; i++) {
    pos = DumpByte(buf, pos, cap, proto->Upvalues[i].InStack);
    pos = DumpByte(buf, pos, cap, proto->Upvalues[i].Index);
  }

  pos = DumpU32(buf, pos, cap, (U32)proto->ProtosSize, endian);
  for (i = 0; i < proto->ProtosSize; i++) {
    pos = DumpProto(L, proto->Protos[i], buf, pos, cap, endian);
  }

  pos = DumpU32(buf, pos, cap, (U32)proto->LineInfoSize, endian);
  pos = DumpBytes(buf, pos, cap, proto->LineInfo, proto->LineInfoSize);

  pos = DumpU32(buf, pos, cap, (U32)proto->LineMapSize, endian);
  for (i = 0; i < proto->LineMapSize; i++) {
    pos = DumpU32(buf, pos, cap, (U32)proto->LineMap[i].PC, endian);
    pos = DumpU32(buf, pos, cap, (U32)proto->LineMap[i].Line, endian);
  }

  return pos;
}

static Bool ProtoFits(MLuaProto *proto) {
  Size i;
  if (!FitsU32(proto->LineDefined) || !FitsU32(proto->CodeSize) ||
      !FitsU32(proto->ConstantsSize) || !FitsU32(proto->UpvaluesSize) ||
      !FitsU32(proto->ProtosSize) || !FitsU32(proto->LineInfoSize) ||
      !FitsU32(proto->LineMapSize)) {
    return FALSE;
  }
  if (IsAnyString(proto->Source) && !FitsU32(MLuaStringLen(proto->Source))) {
    return FALSE;
  }
  for (i = 0; i < proto->ConstantsSize; i++) {
    MLuaValue v = proto->Constants[i];
    if (IsAnyString(v) && !FitsU32(MLuaStringLen(v))) {
      return FALSE;
    }
  }
  for (i = 0; i < proto->LineMapSize; i++) {
    if (!FitsU32(proto->LineMap[i].PC) || !FitsU32(proto->LineMap[i].Line)) {
      return FALSE;
    }
  }
  for (i = 0; i < proto->ProtosSize; i++) {
    if (!ProtoFits(proto->Protos[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

Size MLuaDumpFunctionEndian(MLuaState *L, MLuaValue func, char *buf, Size cap,
                            int endian) {
  MLuaClosure *cl;
  MLuaProto *proto;
  Size pos = 0;

  if (endian != MLUA_BYTECODE_ENDIAN_LITTLE &&
      endian != MLUA_BYTECODE_ENDIAN_BIG) {
    return 0;
  }
  if (!IsPtr(func)) {
    return 0;
  }

  cl = (MLuaClosure *)GetPtr(func);
  proto = cl->Proto;
  if (!proto || !ProtoFits(proto)) {
    return 0;
  }

  pos = DumpByte(buf, pos, cap, 0x1B);
  pos = DumpByte(buf, pos, cap, 'M');
  pos = DumpByte(buf, pos, cap, 'L');
  pos = DumpByte(buf, pos, cap, 'u');
  pos = DumpByte(buf, pos, cap, MLUA_BYTECODE_VERSION);
  pos = DumpByte(buf, pos, cap, BC_FORMAT_OFFICIAL);
  pos = DumpByte(buf, pos, cap, (U8)endian);
  pos = DumpByte(buf, pos, cap, BC_FLAGS_NONE);
  pos = DumpByte(buf, pos, cap, BC_INT_SIZE);
  pos = DumpByte(buf, pos, cap, BC_SIZE_FIELD_SIZE);
  pos = DumpByte(buf, pos, cap, BC_INSTRUCTION_SIZE);
  pos = DumpByte(buf, pos, cap, BC_NUMBER_SIZE);
  pos = DumpByte(buf, pos, cap, MLUA_BYTECODE_FLOAT_IEEE754);

  return DumpProto(L, proto, buf, pos, cap, endian);
}

Size MLuaDumpFunction(MLuaState *L, MLuaValue func, char *buf, Size cap) {
  return MLuaDumpFunctionEndian(L, func, buf, cap,
                               MLUA_BYTECODE_ENDIAN_LITTLE);
}
