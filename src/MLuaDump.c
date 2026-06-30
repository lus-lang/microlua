/*
 * MicroLua - MLuaDump.c
 * Bytecode serialization
 */

#include "MLuaDump.h"
#include "MLuaFunc.h"
#include "MLuaString.h"

/*
 * MicroLua bytecode format:
 * [4] Magic: "\x1bMLu"
 * [1] Version: 0x01
 * [1] Format: 0x00 (official)
 * [1] Endian: 0x01 (little)
 * [1] Int size: sizeof(int)
 * [1] Size_t size: sizeof(Size)
 * [1] Instruction size: 1 (variable)
 * [1] Number size: 8 (double)
 * Then follows the function prototype.
 */

static Size DumpByte(char *buf, Size pos, Size cap, U8 b) {
  if (buf && pos < cap) {
    buf[pos] = (char)b;
  }
  return pos + 1;
}

static Size DumpInt(char *buf, Size pos, Size cap, U32 val) {
  int i;
  for (i = 0; i < 4; i++) {
    if (buf && pos < cap) {
      buf[pos] = (char)(val & 0xFF);
    }
    pos++;
    val >>= 8;
  }
  return pos;
}

static Size DumpSize(char *buf, Size pos, Size cap, Size val) {
  Size i;
  for (i = 0; i < sizeof(Size); i++) {
    if (buf && pos < cap) {
      buf[pos] = (char)(val & 0xFF);
    }
    pos++;
    val >>= 8;
  }
  return pos;
}

static Size DumpBytes(char *buf, Size pos, Size cap, const U8 *data, Size len) {
  Size i;
  for (i = 0; i < len; i++) {
    if (buf && pos < cap) {
      buf[pos] = (char)data[i];
    }
    pos++;
  }
  return pos;
}

static Size DumpNumber(char *buf, Size pos, Size cap, double d) {
  union {
    double d;
    U8 b[8];
  } u;
  Size i;
  u.d = d;
  for (i = 0; i < 8; i++) {
    if (buf && pos < cap) {
      buf[pos] = (char)u.b[i];
    }
    pos++;
  }
  return pos;
}

static Size DumpProto(MLuaState *L, MLuaProto *proto, char *buf, Size pos,
                      Size cap);

static Size DumpValue(MLuaState *L, MLuaValue v, char *buf, Size pos,
                      Size cap) {
  UNUSED(L);
  if (IsNil(v)) {
    pos = DumpByte(buf, pos, cap, 0);
  } else if (IsFalse(v)) {
    pos = DumpByte(buf, pos, cap, 1);
  } else if (IsTrue(v)) {
    pos = DumpByte(buf, pos, cap, 2);
  } else if (IsInt(v)) {
    pos = DumpByte(buf, pos, cap, 3);
    pos = DumpInt(buf, pos, cap, (U32)GetInt(v));
  } else if (MLuaIsNumber(v)) {
    pos = DumpByte(buf, pos, cap, 4);
    pos = DumpNumber(buf, pos, cap, MLuaToNumber(v));
  } else {
    const char *s = MLuaStringData(v);
    Size slen = MLuaStringLen(v);
    pos = DumpByte(buf, pos, cap, 5);
    pos = DumpSize(buf, pos, cap, slen);
    pos = DumpBytes(buf, pos, cap, (const U8 *)s, slen);
  }
  return pos;
}

static Size DumpProto(MLuaState *L, MLuaProto *proto, char *buf, Size pos,
                      Size cap) {
  Size i;

  pos = DumpValue(L, proto->Source, buf, pos, cap);
  pos = DumpInt(buf, pos, cap, (U32)proto->LineDefined);

  pos = DumpByte(buf, pos, cap, proto->NumParams);
  pos = DumpByte(buf, pos, cap, proto->IsVararg);
  pos = DumpByte(buf, pos, cap, proto->MaxStackSize);

  pos = DumpSize(buf, pos, cap, proto->CodeSize);
  pos = DumpBytes(buf, pos, cap, proto->Code, proto->CodeSize);

  pos = DumpSize(buf, pos, cap, proto->ConstantsSize);
  for (i = 0; i < proto->ConstantsSize; i++) {
    pos = DumpValue(L, proto->Constants[i], buf, pos, cap);
  }

  pos = DumpSize(buf, pos, cap, proto->UpvaluesSize);
  for (i = 0; i < proto->UpvaluesSize; i++) {
    pos = DumpByte(buf, pos, cap, proto->Upvalues[i].InStack);
    pos = DumpByte(buf, pos, cap, proto->Upvalues[i].Index);
  }

  pos = DumpSize(buf, pos, cap, proto->ProtosSize);
  for (i = 0; i < proto->ProtosSize; i++) {
    pos = DumpProto(L, proto->Protos[i], buf, pos, cap);
  }

  return pos;
}

Size MLuaDumpFunction(MLuaState *L, MLuaValue func, char *buf, Size cap) {
  MLuaClosure *cl;
  MLuaProto *proto;
  Size pos = 0;

  if (!IsPtr(func)) {
    return 0;
  }

  cl = (MLuaClosure *)GetPtr(func);
  proto = cl->Proto;
  if (!proto) {
    return 0;
  }

  pos = DumpByte(buf, pos, cap, 0x1B);
  pos = DumpByte(buf, pos, cap, 'M');
  pos = DumpByte(buf, pos, cap, 'L');
  pos = DumpByte(buf, pos, cap, 'u');
  pos = DumpByte(buf, pos, cap, 0x01);
  pos = DumpByte(buf, pos, cap, 0x00);
  pos = DumpByte(buf, pos, cap, 0x01);
  pos = DumpByte(buf, pos, cap, sizeof(int));
  pos = DumpByte(buf, pos, cap, sizeof(Size));
  pos = DumpByte(buf, pos, cap, 1);
  pos = DumpByte(buf, pos, cap, 8);

  return DumpProto(L, proto, buf, pos, cap);
}
