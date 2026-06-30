/*
 * MicroLua - MLuaStringLib.c
 * String library implementation
 */

#include "MLuaStringLib.h"
#include "../MLuaCode.h"
#include "../MLuaConvert.h"
#include "../MLuaCore.h"
#include "../MLuaFunc.h"
#include "../MLuaString.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* Helper: Get string from argument                                           */
/* ========================================================================== */

static const char *GetStrArg(MLuaState *L, int idx, Size *len) {
  MLuaValue v = MLuaGetStack(L, idx);
  if (len)
    *len = MLuaStringLen(v);
  return MLuaStringData(v);
}

/* ========================================================================== */
/* string.byte                                                                */
/* ========================================================================== */

static int StringByte(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  int top = MLuaGetTop(L);
  Size i, j;
  Size count;

  if (!s || len == 0) {
    return 0;
  }

  /* Default: i=1, j=i */
  i = 1;
  j = 1;

  if (top >= 2) {
    MLuaValue vi = MLuaGetStack(L, 2);
    i = (Size)GetInt(vi);
  }
  if (top >= 3) {
    MLuaValue vj = MLuaGetStack(L, 3);
    j = (Size)GetInt(vj);
  } else {
    j = i;
  }

  /* Adjust for 1-based indexing */
  if (i < 1)
    i = 1;
  if (j > len)
    j = len;
  if (i > j || i > len)
    return 0;

  /* Return byte values */
  count = 0;
  for (; i <= j && i <= len; i++) {
    MLuaPush(L, MakeInt((I32)(U8)s[i - 1]));
    count++;
  }
  return (int)count;
}

/* ========================================================================== */
/* string.char                                                                */
/* ========================================================================== */

static int StringChar(MLuaState *L) {
  int top = MLuaGetTop(L);
  char buf[256];
  int i;

  if (top > 256) {
    top = 256;
  }

  for (i = 0; i < top; i++) {
    MLuaValue v = MLuaGetStack(L, i + 1);
    I32 c = GetInt(v);
    buf[i] = (char)(U8)c;
  }

  MLuaPush(L, MLuaStringNew(L, buf, (Size)top));
  return 1;
}

/* ========================================================================== */
/* string.dump                                                                */
/* ========================================================================== */

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
 * Then follows the function prototype
 */

/* Helper: write a byte to buffer */
static Size DumpByte(char *buf, Size pos, Size cap, U8 b) {
  if (pos < cap)
    buf[pos] = (char)b;
  return pos + 1;
}

/* Helper: write a 32-bit int (little endian) */
static Size DumpInt(char *buf, Size pos, Size cap, U32 val) {
  int i;
  for (i = 0; i < 4 && pos < cap; i++) {
    buf[pos++] = (char)(val & 0xFF);
    val >>= 8;
  }
  return pos;
}

/* Helper: write a Size (little endian, 4 or 8 bytes depending on platform) */
static Size DumpSize(char *buf, Size pos, Size cap, Size val) {
  Size i;
  for (i = 0; i < sizeof(Size) && pos < cap; i++) {
    buf[pos++] = (char)(val & 0xFF);
    val >>= 8;
  }
  return pos;
}

/* Helper: write bytes */
static Size DumpBytes(char *buf, Size pos, Size cap, const U8 *data, Size len) {
  Size i;
  for (i = 0; i < len && pos < cap; i++) {
    buf[pos++] = (char)data[i];
  }
  return pos;
}

/* Helper: write a double */
static Size DumpNumber(char *buf, Size pos, Size cap, double d) {
  union {
    double d;
    U8 b[8];
  } u;
  Size i;
  u.d = d;
  for (i = 0; i < 8 && pos < cap; i++) {
    buf[pos++] = (char)u.b[i];
  }
  return pos;
}

/* Forward declaration */
static Size DumpProto(MLuaState *L, MLuaProto *proto, char *buf, Size pos,
                      Size cap);

/* Dump a constant value */
static Size DumpValue(MLuaState *L, MLuaValue v, char *buf, Size pos,
                      Size cap) {
  UNUSED(L);
  if (IsNil(v)) {
    pos = DumpByte(buf, pos, cap, 0); /* type nil */
  } else if (IsFalse(v)) {
    pos = DumpByte(buf, pos, cap, 1); /* type false */
  } else if (IsTrue(v)) {
    pos = DumpByte(buf, pos, cap, 2); /* type true */
  } else if (IsInt(v)) {
    pos = DumpByte(buf, pos, cap, 3); /* type int */
    pos = DumpInt(buf, pos, cap, (U32)GetInt(v));
  } else if (MLuaIsNumber(v)) {
    pos = DumpByte(buf, pos, cap, 4); /* type number */
    pos = DumpNumber(buf, pos, cap, MLuaToNumber(v));
  } else { /* String */
    const char *s = MLuaStringData(v);
    Size slen = MLuaStringLen(v);
    pos = DumpByte(buf, pos, cap, 5); /* type string */
    pos = DumpSize(buf, pos, cap, slen);
    pos = DumpBytes(buf, pos, cap, (const U8 *)s, slen);
  }
  return pos;
}

/* Dump a function prototype */
static Size DumpProto(MLuaState *L, MLuaProto *proto, char *buf, Size pos,
                      Size cap) {
  Size i;

  /* Source (for debug) */
  pos = DumpValue(L, proto->Source, buf, pos, cap);

  /* Line defined */
  pos = DumpInt(buf, pos, cap, (U32)proto->LineDefined);

  /* Params, vararg, maxstack */
  pos = DumpByte(buf, pos, cap, proto->NumParams);
  pos = DumpByte(buf, pos, cap, proto->IsVararg);
  pos = DumpByte(buf, pos, cap, proto->MaxStackSize);

  /* Code */
  pos = DumpSize(buf, pos, cap, proto->CodeSize);
  pos = DumpBytes(buf, pos, cap, proto->Code, proto->CodeSize);

  /* Constants */
  pos = DumpSize(buf, pos, cap, proto->ConstantsSize);
  for (i = 0; i < proto->ConstantsSize; i++) {
    pos = DumpValue(L, proto->Constants[i], buf, pos, cap);
  }

  /* Upvalues */
  pos = DumpSize(buf, pos, cap, proto->UpvaluesSize);
  for (i = 0; i < proto->UpvaluesSize; i++) {
    pos = DumpByte(buf, pos, cap, proto->Upvalues[i].InStack);
    pos = DumpByte(buf, pos, cap, proto->Upvalues[i].Index);
  }

  /* Nested prototypes */
  pos = DumpSize(buf, pos, cap, proto->ProtosSize);
  for (i = 0; i < proto->ProtosSize; i++) {
    pos = DumpProto(L, proto->Protos[i], buf, pos, cap);
  }

  return pos;
}

static int StringDump(MLuaState *L) {
  MLuaValue func = MLuaGetStack(L, 1);
  char buf[16384];
  Size pos = 0;
  Size cap = sizeof(buf);
  MLuaClosure *cl;
  MLuaProto *proto;

  /* Check if it's a function */
  if (!IsPtr(func)) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "cannot dump C functions", 23));
    return 2;
  }

  cl = (MLuaClosure *)GetPtr(func);
  proto = cl->Proto;

  if (!proto) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "cannot dump C functions", 23));
    return 2;
  }

  /* Write header */
  pos = DumpByte(buf, pos, cap, 0x1B); /* ESC */
  pos = DumpByte(buf, pos, cap, 'M');
  pos = DumpByte(buf, pos, cap, 'L');
  pos = DumpByte(buf, pos, cap, 'u');
  pos = DumpByte(buf, pos, cap, 0x01); /* Version */
  pos = DumpByte(buf, pos, cap, 0x00); /* Format */
  pos = DumpByte(buf, pos, cap, 0x01); /* Little endian */
  pos = DumpByte(buf, pos, cap, sizeof(int));
  pos = DumpByte(buf, pos, cap, sizeof(Size));
  pos = DumpByte(buf, pos, cap, 1); /* Instruction size (variable) */
  pos = DumpByte(buf, pos, cap, 8); /* Number size (double) */

  /* Dump prototype */
  pos = DumpProto(L, proto, buf, pos, cap);

  MLuaPush(L, MLuaStringNew(L, buf, pos));
  return 1;
}

/* ========================================================================== */
/* string.find (with Lua pattern matching)                                    */
/* ========================================================================== */

/* Pattern matching state */
typedef struct {
  const char *SrcStart;
  const char *SrcEnd;
  const char *PatStart;
  const char *PatEnd;
  const char *CapStarts[32];
  const char *CapEnds[32];
  int NumCaptures;
} MatchState;

/* Check if character matches class c */
static Bool MatchClass(char c, char cl) {
  Bool res;
  switch (cl) {
  case 'a':
    res = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    break;
  case 'd':
    res = (c >= '0' && c <= '9');
    break;
  case 's':
    res = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v');
    break;
  case 'w':
    res = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_';
    break;
  case 'l':
    res = (c >= 'a' && c <= 'z');
    break;
  case 'u':
    res = (c >= 'A' && c <= 'Z');
    break;
  case 'p':
    res = (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
          (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
    break;
  case 'c':
    res = ((unsigned char)c < 32 || c == 127);
    break;
  case 'x':
    res = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F');
    break;
  default:
    res = FALSE;
    break;
  }
  return res;
}

/* Match single pattern item against character */
static Bool MatchSingle(MatchState *ms, const char *p, char c) {
  UNUSED(ms);
  if (*p == '.')
    return TRUE; /* Matches any char */
  if (*p == '%') {
    p++;
    /* Check for uppercase (negated) class */
    if (*p >= 'A' && *p <= 'Z') {
      char lower = *p + 32;
      return !MatchClass(c, lower);
    }
    return MatchClass(c, *p);
  }
  if (*p == '[') {
    /* Character set - scan until ] */
    const char *end = p + 1;
    Bool negate = FALSE;
    if (*end == '^') {
      negate = TRUE;
      end++;
    }
    while (*end && *end != ']') {
      if (end[1] == '-' && end[2] && end[2] != ']') {
        /* Range a-z */
        if (c >= end[0] && c <= end[2])
          return !negate;
        end += 3;
      } else {
        if (c == *end)
          return !negate;
        end++;
      }
    }
    return negate;
  }
  return *p == c;
}

/* Get pattern item length (1, 2 for %, or to ] for char set) */
static Size PatItemLen(const char *p) {
  if (*p == '%')
    return 2;
  if (*p == '[') {
    const char *end = p + 1;
    if (*end == '^')
      end++;
    while (*end && *end != ']')
      end++;
    return (Size)(end - p + 1);
  }
  return 1;
}

/* Forward declaration */
static const char *PatMatch(MatchState *ms, const char *s, const char *p);

/* Match repeated pattern item */
static const char *MatchRepeat(MatchState *ms, const char *s, const char *p,
                               Size itemLen, Bool greedy) {
  Size count = 0;
  const char *m;

  if (greedy) {
    /* Greedy: match as many as possible then backtrack */
    while (s + count < ms->SrcEnd && MatchSingle(ms, p, s[count]))
      count++;
    while (count >= 0) {
      m = PatMatch(ms, s + count, p + itemLen + 1);
      if (m)
        return m;
      if (count == 0)
        break;
      count--;
    }
  } else {
    /* Non-greedy: match as few as possible */
    while (1) {
      m = PatMatch(ms, s + count, p + itemLen + 1);
      if (m)
        return m;
      if (s + count >= ms->SrcEnd || !MatchSingle(ms, p, s[count]))
        break;
      count++;
    }
  }
  return NULL;
}

/* Main pattern matching function */
static const char *PatMatch(MatchState *ms, const char *s, const char *p) {
  while (p < ms->PatEnd) {
    Size itemLen = PatItemLen(p);
    char next = (p + itemLen < ms->PatEnd) ? p[itemLen] : '\0';

    /* Handle anchors */
    if (*p == '$' && p + 1 == ms->PatEnd) {
      return (s == ms->SrcEnd) ? s : NULL;
    }

    /* Handle captures */
    if (*p == '(') {
      ms->CapStarts[ms->NumCaptures] = s;
      p++;
      continue;
    }
    if (*p == ')') {
      ms->CapEnds[ms->NumCaptures++] = s;
      p++;
      continue;
    }

    /* Handle quantifiers */
    if (next == '*') {
      return MatchRepeat(ms, s, p, itemLen, TRUE);
    }
    if (next == '+') {
      if (s >= ms->SrcEnd || !MatchSingle(ms, p, *s))
        return NULL;
      return MatchRepeat(ms, s + 1, p, itemLen, TRUE);
    }
    if (next == '-') {
      return MatchRepeat(ms, s, p, itemLen, FALSE);
    }
    if (next == '?') {
      const char *m = PatMatch(ms, s, p + itemLen + 1);
      if (m)
        return m;
      if (s < ms->SrcEnd && MatchSingle(ms, p, *s)) {
        return PatMatch(ms, s + 1, p + itemLen + 1);
      }
      return NULL;
    }

    /* Single match */
    if (s >= ms->SrcEnd || !MatchSingle(ms, p, *s))
      return NULL;
    s++;
    p += itemLen;
  }
  return s;
}

static int StringFind(MLuaState *L) {
  Size slen, plen;
  const char *s = GetStrArg(L, 1, &slen);
  const char *pattern = GetStrArg(L, 2, &plen);
  Size init = 1;
  Bool plain = FALSE;
  MatchState ms;
  Size i;

  if (!s || !pattern) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  if (MLuaGetTop(L) >= 3) {
    MLuaValue vi = MLuaGetStack(L, 3);
    init = (Size)GetInt(vi);
  }
  if (MLuaGetTop(L) >= 4) {
    MLuaValue vp = MLuaGetStack(L, 4);
    plain = IsTruthy(vp);
  }

  if (init < 1)
    init = 1;
  if (init > slen) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Plain text search */
  if (plain) {
    for (i = init - 1; i + plen <= slen; i++) {
      Size j;
      Bool match = TRUE;
      for (j = 0; j < plen; j++) {
        if (s[i + j] != pattern[j]) {
          match = FALSE;
          break;
        }
      }
      if (match) {
        MLuaPush(L, MakeInt((I32)(i + 1)));
        MLuaPush(L, MakeInt((I32)(i + plen)));
        return 2;
      }
    }
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Pattern matching */
  ms.SrcStart = s;
  ms.SrcEnd = s + slen;
  ms.PatStart = pattern;
  ms.PatEnd = pattern + plen;
  ms.NumCaptures = 0;

  /* Check for anchor */
  if (*pattern == '^') {
    const char *end = PatMatch(&ms, s + init - 1, pattern + 1);
    if (end) {
      MLuaPush(L, MakeInt((I32)init));
      MLuaPush(L, MakeInt((I32)(init + (end - (s + init - 1)) - 1)));
      /* Push captures */
      for (i = 0; i < (Size)ms.NumCaptures; i++) {
        MLuaPush(L, MLuaStringNew(L, ms.CapStarts[i],
                                  (Size)(ms.CapEnds[i] - ms.CapStarts[i])));
      }
      return 2 + ms.NumCaptures;
    }
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Try matching at each position */
  for (i = init - 1; i < slen; i++) {
    ms.NumCaptures = 0;
    const char *end = PatMatch(&ms, s + i, pattern);
    if (end) {
      Size j;
      MLuaPush(L, MakeInt((I32)(i + 1)));
      MLuaPush(L, MakeInt((I32)(i + 1 + (end - (s + i)) - 1)));
      /* Push captures */
      for (j = 0; j < (Size)ms.NumCaptures; j++) {
        MLuaPush(L, MLuaStringNew(L, ms.CapStarts[j],
                                  (Size)(ms.CapEnds[j] - ms.CapStarts[j])));
      }
      return 2 + ms.NumCaptures;
    }
  }

  MLuaPush(L, MLUA_NIL);
  return 1;
}

/* ========================================================================== */
/* string.format                                                              */
/* ========================================================================== */

static int StringFormat(MLuaState *L) {
  Size fmtlen;
  const char *fmt = GetStrArg(L, 1, &fmtlen);
  char buf[4096];
  int top = MLuaGetTop(L);
  MLuaValue args[32];
  int nargs = 0;
  int i;

  if (!fmt) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Gather arguments */
  for (i = 2; i <= top && nargs < 32; i++) {
    args[nargs++] = MLuaGetStack(L, i);
  }

  Size len = MLuaFormat(L, fmt, fmtlen, args, nargs, buf, sizeof(buf));
  MLuaPush(L, MLuaStringNew(L, buf, len));
  return 1;
}

/* ========================================================================== */
/* string.len                                                                 */
/* ========================================================================== */

static int StringLen(MLuaState *L) {
  Size len;
  GetStrArg(L, 1, &len);
  MLuaPush(L, MakeInt((I32)len));
  return 1;
}

/* ========================================================================== */
/* string.lower                                                               */
/* ========================================================================== */

static int StringLower(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  char buf[1024];
  Size i;

  if (!s || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  if (len > sizeof(buf)) {
    len = sizeof(buf);
  }

  for (i = 0; i < len; i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') {
      buf[i] = c + 32;
    } else {
      buf[i] = c;
    }
  }

  MLuaPush(L, MLuaStringNew(L, buf, len));
  return 1;
}

/* ========================================================================== */
/* string.match                                                               */
/* ========================================================================== */

static int StringMatch(MLuaState *L) {
  /* Pattern matching - returns captures or whole match */
  Size slen, plen;
  const char *s = GetStrArg(L, 1, &slen);
  const char *pattern = GetStrArg(L, 2, &plen);
  Size init = 1;
  MatchState ms;
  Size i;

  if (!s || !pattern) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  if (MLuaGetTop(L) >= 3) {
    MLuaValue vi = MLuaGetStack(L, 3);
    init = (Size)GetInt(vi);
  }

  if (init < 1)
    init = 1;
  if (init > slen) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Pattern matching */
  ms.SrcStart = s;
  ms.SrcEnd = s + slen;
  ms.PatStart = pattern;
  ms.PatEnd = pattern + plen;
  ms.NumCaptures = 0;

  /* Check for anchor */
  if (*pattern == '^') {
    const char *end = PatMatch(&ms, s + init - 1, pattern + 1);
    if (end) {
      if (ms.NumCaptures > 0) {
        /* Return captures */
        for (i = 0; i < (Size)ms.NumCaptures; i++) {
          MLuaPush(L, MLuaStringNew(L, ms.CapStarts[i],
                                    (Size)(ms.CapEnds[i] - ms.CapStarts[i])));
        }
        return ms.NumCaptures;
      } else {
        /* Return whole match */
        MLuaPush(L,
                 MLuaStringNew(L, s + init - 1, (Size)(end - (s + init - 1))));
        return 1;
      }
    }
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Try matching at each position */
  for (i = init - 1; i < slen; i++) {
    ms.NumCaptures = 0;
    const char *end = PatMatch(&ms, s + i, pattern);
    if (end) {
      if (ms.NumCaptures > 0) {
        /* Return captures */
        Size j;
        for (j = 0; j < (Size)ms.NumCaptures; j++) {
          MLuaPush(L, MLuaStringNew(L, ms.CapStarts[j],
                                    (Size)(ms.CapEnds[j] - ms.CapStarts[j])));
        }
        return ms.NumCaptures;
      } else {
        /* Return whole match */
        MLuaPush(L, MLuaStringNew(L, s + i, (Size)(end - (s + i))));
        return 1;
      }
    }
  }

  MLuaPush(L, MLUA_NIL);
  return 1;
}

/* ========================================================================== */
/* string.pack                                                                */
/* ========================================================================== */

static int StringPack(MLuaState *L) {
  /*
   * Format specifiers:
   * b/B = signed/unsigned byte
   * h/H = signed/unsigned short (2 bytes)
   * i/I = signed/unsigned int (4 bytes)
   * l/L = signed/unsigned long (8 bytes)
   * n/N = native size_t / ssize_t
   * cn = n bytes from string
   * s = string with length prefix
   * z = null-terminated string
   */
  Size fmtlen;
  const char *fmt = GetStrArg(L, 1, &fmtlen);
  char buf[1024];
  Size pos = 0;
  Size fi = 0;
  int argIdx = 2;

  if (!fmt) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  while (fi < fmtlen && pos < sizeof(buf) - 8) {
    char c = fmt[fi++];

    switch (c) {
    case 'b': { /* signed byte */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      I8 val = (I8)GetInt(v);
      buf[pos++] = (char)val;
      break;
    }
    case 'B': { /* unsigned byte */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U8 val = (U8)GetInt(v);
      buf[pos++] = (char)val;
      break;
    }
    case 'h': { /* signed short */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      I16 val = (I16)GetInt(v);
      buf[pos++] = (char)(val & 0xFF);
      buf[pos++] = (char)((val >> 8) & 0xFF);
      break;
    }
    case 'H': { /* unsigned short */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U16 val = (U16)GetInt(v);
      buf[pos++] = (char)(val & 0xFF);
      buf[pos++] = (char)((val >> 8) & 0xFF);
      break;
    }
    case 'i':
    case 'I': { /* signed/unsigned int */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U32 val = (U32)GetInt(v);
      buf[pos++] = (char)(val & 0xFF);
      buf[pos++] = (char)((val >> 8) & 0xFF);
      buf[pos++] = (char)((val >> 16) & 0xFF);
      buf[pos++] = (char)((val >> 24) & 0xFF);
      break;
    }
    case 'l':
    case 'L':
    case 'n':
    case 'N': { /* 8-byte */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U64 val = IsInt(v) ? (U64)(U32)GetInt(v) : (U64)MLuaToNumber(v);
      int i;
      for (i = 0; i < 8; i++) {
        buf[pos++] = (char)((val >> (i * 8)) & 0xFF);
      }
      break;
    }
    case 'c': { /* fixed-size string: cn */
      int n = 0;
      while (fi < fmtlen && fmt[fi] >= '0' && fmt[fi] <= '9') {
        n = n * 10 + (fmt[fi++] - '0');
      }
      if (n > 0) {
        Size slen;
        const char *s = GetStrArg(L, argIdx++, &slen);
        Size i;
        for (i = 0; i < (Size)n && pos < sizeof(buf); i++) {
          buf[pos++] = (i < slen && s) ? s[i] : '\0';
        }
      }
      break;
    }
    case 'z': { /* null-terminated string */
      Size slen;
      const char *s = GetStrArg(L, argIdx++, &slen);
      Size i;
      for (i = 0; i < slen && s && pos < sizeof(buf) - 1; i++) {
        buf[pos++] = s[i];
      }
      buf[pos++] = '\0';
      break;
    }
    case ' ':
    case '<':
    case '>':
    case '=':
      /* Skip alignment/endianness markers */
      break;
    default:
      break;
    }
  }

  MLuaPush(L, MLuaStringNew(L, buf, pos));
  return 1;
}

/* ========================================================================== */
/* string.packsize                                                            */
/* ========================================================================== */

static int StringPacksize(MLuaState *L) {
  Size fmtlen;
  const char *fmt = GetStrArg(L, 1, &fmtlen);
  Size size = 0;
  Size fi = 0;

  if (!fmt) {
    MLuaPush(L, MakeInt(0));
    return 1;
  }

  while (fi < fmtlen) {
    char c = fmt[fi++];

    switch (c) {
    case 'b':
    case 'B':
      size += 1;
      break;
    case 'h':
    case 'H':
      size += 2;
      break;
    case 'i':
    case 'I':
      size += 4;
      break;
    case 'l':
    case 'L':
    case 'n':
    case 'N':
      size += 8;
      break;
    case 'c': {
      int n = 0;
      while (fi < fmtlen && fmt[fi] >= '0' && fmt[fi] <= '9') {
        n = n * 10 + (fmt[fi++] - '0');
      }
      size += (Size)n;
      break;
    }
    case 'z': /* Variable length - cannot compute */
    case 's':
      MLuaPush(L, MLUA_NIL);
      MLuaPush(L, MLuaStringNew(L, "variable length format", 22));
      return 2;
    default:
      break;
    }
  }

  MLuaPush(L, MakeInt((I32)size));
  return 1;
}

/* ========================================================================== */
/* string.rep                                                                 */
/* ========================================================================== */

static int StringRep(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  MLuaValue vn = MLuaGetStack(L, 2);
  I32 n = GetInt(vn);
  char buf[1024];
  Size bufpos = 0;
  I32 i;

  if (!s || n <= 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  for (i = 0; i < n && bufpos + len < sizeof(buf); i++) {
    Size j;
    for (j = 0; j < len; j++) {
      buf[bufpos++] = s[j];
    }
  }

  MLuaPush(L, MLuaStringNew(L, buf, bufpos));
  return 1;
}

/* ========================================================================== */
/* string.reverse                                                             */
/* ========================================================================== */

static int StringReverse(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  char buf[1024];
  Size i;

  if (!s || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  if (len > sizeof(buf)) {
    len = sizeof(buf);
  }

  for (i = 0; i < len; i++) {
    buf[i] = s[len - 1 - i];
  }

  MLuaPush(L, MLuaStringNew(L, buf, len));
  return 1;
}

/* ========================================================================== */
/* string.sub                                                                 */
/* ========================================================================== */

static int StringSub(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  IPtr i = 1;
  IPtr j = -1;

  if (MLuaGetTop(L) >= 2) {
    MLuaValue vi = MLuaGetStack(L, 2);
    i = GetInt(vi);
  }
  if (MLuaGetTop(L) >= 3) {
    MLuaValue vj = MLuaGetStack(L, 3);
    j = GetInt(vj);
  }

  if (!s) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Handle negative indices */
  if (i < 0)
    i = (IPtr)len + i + 1;
  if (j < 0)
    j = (IPtr)len + j + 1;

  /* Clamp */
  if (i < 1)
    i = 1;
  if (j > (IPtr)len)
    j = (IPtr)len;

  if (i > j) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
  } else {
    MLuaPush(L, MLuaStringNew(L, s + i - 1, (Size)(j - i + 1)));
  }
  return 1;
}

/* ========================================================================== */
/* string.unpack                                                              */
/* ========================================================================== */

static int StringUnpack(MLuaState *L) {
  Size fmtlen, datalen;
  const char *fmt = GetStrArg(L, 1, &fmtlen);
  const char *data = GetStrArg(L, 2, &datalen);
  Size pos = 1; /* Starting position (1-indexed, convert to 0-indexed) */
  Size fi = 0;
  int count = 0;

  if (MLuaGetTop(L) >= 3) {
    MLuaValue vpos = MLuaGetStack(L, 3);
    pos = (Size)GetInt(vpos);
  }

  if (!fmt || !data) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Convert to 0-indexed */
  if (pos < 1)
    pos = 1;
  pos--;

  while (fi < fmtlen && pos < datalen) {
    char c = fmt[fi++];

    switch (c) {
    case 'b': { /* signed byte */
      if (pos >= datalen)
        break;
      I8 val = (I8)(U8)data[pos++];
      MLuaPush(L, MakeInt(val));
      count++;
      break;
    }
    case 'B': { /* unsigned byte */
      if (pos >= datalen)
        break;
      U8 val = (U8)data[pos++];
      MLuaPush(L, MakeInt(val));
      count++;
      break;
    }
    case 'h': { /* signed short */
      if (pos + 1 >= datalen)
        break;
      I16 val = (I16)((U8)data[pos] | ((U8)data[pos + 1] << 8));
      pos += 2;
      MLuaPush(L, MakeInt(val));
      count++;
      break;
    }
    case 'H': { /* unsigned short */
      if (pos + 1 >= datalen)
        break;
      U16 val = (U16)((U8)data[pos] | ((U8)data[pos + 1] << 8));
      pos += 2;
      MLuaPush(L, MakeInt(val));
      count++;
      break;
    }
    case 'i':
    case 'I': { /* 4-byte int */
      if (pos + 3 >= datalen)
        break;
      U32 val = (U32)((U8)data[pos] | ((U8)data[pos + 1] << 8) |
                      ((U8)data[pos + 2] << 16) | ((U8)data[pos + 3] << 24));
      pos += 4;
      MLuaPush(L, MakeInt((I32)val));
      count++;
      break;
    }
    case 'l':
    case 'L':
    case 'n':
    case 'N': { /* 8-byte */
      if (pos + 7 >= datalen)
        break;
      U64 val = 0;
      int i;
      for (i = 0; i < 8; i++) {
        val |= ((U64)(U8)data[pos + i]) << (i * 8);
      }
      pos += 8;
      /* Convert to double for large values, int for small */
      if (val <= 0x7FFFFFFF) {
        MLuaPush(L, MakeInt((I32)val));
      } else {
        MLuaPush(L, MLuaMakeNumber(L, (double)val));
      }
      count++;
      break;
    }
    case 'c': { /* fixed-size string: cn */
      int n = 0;
      while (fi < fmtlen && fmt[fi] >= '0' && fmt[fi] <= '9') {
        n = n * 10 + (fmt[fi++] - '0');
      }
      if (n > 0 && pos + (Size)n <= datalen) {
        MLuaPush(L, MLuaStringNew(L, data + pos, (Size)n));
        pos += (Size)n;
        count++;
      }
      break;
    }
    case 'z': { /* null-terminated string */
      Size start = pos;
      while (pos < datalen && data[pos] != '\0')
        pos++;
      MLuaPush(L, MLuaStringNew(L, data + start, pos - start));
      if (pos < datalen)
        pos++; /* Skip null */
      count++;
      break;
    }
    case ' ':
    case '<':
    case '>':
    case '=':
      /* Skip alignment/endianness markers */
      break;
    default:
      break;
    }
  }

  /* Return position as last value (1-indexed) */
  MLuaPush(L, MakeInt((I32)(pos + 1)));
  return count + 1;
}

/* ========================================================================== */
/* string.upper                                                               */
/* ========================================================================== */

static int StringUpper(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  char buf[1024];
  Size i;

  if (!s || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  if (len > sizeof(buf)) {
    len = sizeof(buf);
  }

  for (i = 0; i < len; i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') {
      buf[i] = c - 32;
    } else {
      buf[i] = c;
    }
  }

  MLuaPush(L, MLuaStringNew(L, buf, len));
  return 1;
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

static const MLuaLibEntry StringLibEntries[] = {
    {"byte", StringByte},     {"char", StringChar},
    {"dump", StringDump},     {"find", StringFind},
    {"format", StringFormat}, {"len", StringLen},
    {"lower", StringLower},   {"match", StringMatch},
    {"pack", StringPack},     {"packsize", StringPacksize},
    {"rep", StringRep},       {"reverse", StringReverse},
    {"sub", StringSub},       {"unpack", StringUnpack},
    {"upper", StringUpper},   {NULL, NULL}};

void MLuaOpenString(MLuaState *L) {
  MLuaValue lib = MLuaNewLib(L, "string");
  MLuaRegisterLib(L, lib, StringLibEntries);
}
