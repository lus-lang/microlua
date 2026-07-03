/*
 * MicroLua - MLuaStringLib.c
 * String library implementation
 */

#include "MLuaStringLib.h"
#include "../MLuaCode.h"
#include "../MLuaConvert.h"
#include "../MLuaCore.h"
#include "../MLuaDump.h"
#include "../MLuaFunc.h"
#include "../MLuaString.h"
#include "../MLuaUTF8.h"
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

/*
 * Push a built string result, or fail the C call when its creation failed
 * (MLuaStringNew sets ErrorMsg and returns nil on allocation failure; a
 * bare push of that nil would report success with a wrong value).
 */
static int PushBuiltString(MLuaState *L, MLuaValue v) {
  if (IsNil(v)) {
    return -1;
  }
  MLuaPush(L, v);
  return 1;
}

/* ========================================================================== */
/* string.byte                                                                */
/* ========================================================================== */

static int StringByte(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  Bool ascii = MLuaStringIsAscii(MLuaGetStack(L, 1));
  int top = MLuaGetTop(L);
  IPtr i = 1;
  IPtr j;
  IPtr total;
  const char *p;
  const char *end;
  Size count;

  if (!s || len == 0) {
    return 0;
  }

  if (top >= 2) {
    MLuaValue vi = MLuaGetStack(L, 2);
    i = MLuaGetIntVal(vi);
  }
  if (top >= 3) {
    MLuaValue vj = MLuaGetStack(L, 3);
    j = MLuaGetIntVal(vj);
  } else {
    j = i;
  }

  /* Unicode-aware: positions are codepoint indices, results are CODEPOINTS.
   * On an all-ASCII string codepoints ARE bytes, so skip the decoding. */
  total = ascii ? (IPtr)len : (IPtr)MLuaUTF8Len(s, len);
  if (i < 0)
    i = total + i + 1;
  if (j < 0)
    j = total + j + 1;
  if (i < 1)
    i = 1;
  if (j > total)
    j = total;
  if (i > j)
    return 0;

  if (ascii) {
    count = 0;
    while (i <= j) {
      MLuaPush(L, MakeInt((I32)(U8)s[i - 1]));
      count++;
      i++;
    }
    return (int)count;
  }

  p = s + MLuaUTF8Offset(s, len, (Size)(i - 1));
  end = s + len;
  count = 0;
  while (i <= j && p < end) {
    U32 cp;
    Size consumed = MLuaUTF8Decode(p, end, &cp);
    if (consumed == 0) {
      break;
    }
    MLuaPush(L, MakeInt((I32)cp));
    count++;
    p += consumed;
    i++;
  }
  return (int)count;
}

/* ========================================================================== */
/* string.char                                                                */
/* ========================================================================== */

static int StringChar(MLuaState *L) {
  int top = MLuaGetTop(L);
  char buf[1024]; /* up to 256 codepoints x 4 UTF-8 bytes */
  Size pos = 0;
  int i;

  if (top > 256) {
    top = 256;
  }

  /* Unicode-aware: arguments are CODEPOINTS, encoded as UTF-8 */
  for (i = 0; i < top; i++) {
    MLuaValue v = MLuaGetStack(L, i + 1);
    U32 cp = (U32)MLuaGetIntVal(v);
    pos += MLuaUTF8Encode(cp, buf + pos);
  }

  return PushBuiltString(L, MLuaStringNew(L, buf, pos));
}

/* ========================================================================== */
/* string.dump                                                                */
/* ========================================================================== */

#if MLUA_ENABLE_DUMP
static int StringDump(MLuaState *L) {
  MLuaValue func = MLuaGetStack(L, 1);
  Size size;
  char *buf;

  size = MLuaDumpFunction(L, func, NULL, 0);
  if (size == 0) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "cannot dump C functions", 23));
    return 2;
  }

  buf = (char *)MLuaAlloc(L, size);
  if (!buf) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "out of memory", 13));
    return 2;
  }

  MLuaDumpFunction(L, func, buf, size);
  return PushBuiltString(L, MLuaStringNew(L, buf, size));
}
#endif /* MLUA_ENABLE_DUMP */

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
  const char *OpenCapStart;
  int NumCaptures;
  int CaptureDepth;
  const char *Error;
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
    /* Not a class letter: '%x' matches the literal character x. This is
     * how %. %% %( %[ %- etc. escape the magic characters (PUC rule). */
    res = (c == cl);
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
      if (ms->CaptureDepth != 0) {
        ms->Error = "nested captures are not supported";
        return NULL;
      }
      if (ms->NumCaptures >= 32) {
        ms->Error = "too many captures";
        return NULL;
      }
      ms->OpenCapStart = s;
      ms->CaptureDepth = 1;
      p++;
      continue;
    }
    if (*p == ')') {
      if (ms->CaptureDepth == 0) {
        ms->Error = "invalid pattern capture";
        return NULL;
      }
      ms->CapStarts[ms->NumCaptures] = ms->OpenCapStart;
      ms->CapEnds[ms->NumCaptures++] = s;
      ms->OpenCapStart = NULL;
      ms->CaptureDepth = 0;
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
  if (ms->CaptureDepth != 0) {
    ms->Error = "unfinished capture";
    return NULL;
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
    init = (Size)MLuaGetIntVal(vi);
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
  ms.CaptureDepth = 0;
  ms.OpenCapStart = NULL;
  ms.Error = NULL;

  /* Check for anchor */
  if (*pattern == '^') {
    const char *end = PatMatch(&ms, s + init - 1, pattern + 1);
    if (ms.Error) {
      L->ErrorMsg = ms.Error;
      return -1;
    }
    if (end) {
      MLuaPush(L, MakeInt((I32)init));
      MLuaPush(L, MakeInt((I32)(init + (end - (s + init - 1)) - 1)));
      /* Push captures */
      for (i = 0; i < (Size)ms.NumCaptures; i++) {
        MLuaValue cap = MLuaStringNew(L, ms.CapStarts[i],
                                      (Size)(ms.CapEnds[i] - ms.CapStarts[i]));
        if (IsNil(cap)) {
          return -1;
        }
        MLuaPush(L, cap);
      }
      return 2 + ms.NumCaptures;
    }
    MLuaPush(L, MLUA_NIL);
    return 1;
  }

  /* Try matching at each position */
  for (i = init - 1; i < slen; i++) {
    ms.NumCaptures = 0;
    ms.CaptureDepth = 0;
    ms.OpenCapStart = NULL;
    ms.Error = NULL;
    const char *end = PatMatch(&ms, s + i, pattern);
    if (ms.Error) {
      L->ErrorMsg = ms.Error;
      return -1;
    }
    if (end) {
      Size j;
      MLuaPush(L, MakeInt((I32)(i + 1)));
      MLuaPush(L, MakeInt((I32)(i + 1 + (end - (s + i)) - 1)));
      /* Push captures */
      for (j = 0; j < (Size)ms.NumCaptures; j++) {
        MLuaValue cap = MLuaStringNew(L, ms.CapStarts[j],
                                      (Size)(ms.CapEnds[j] - ms.CapStarts[j]));
        if (IsNil(cap)) {
          return -1;
        }
        MLuaPush(L, cap);
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
  return PushBuiltString(L, MLuaStringNew(L, buf, len));
}

/* ========================================================================== */
/* string.len                                                                 */
/* ========================================================================== */

static int StringLen(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);

  /* Unicode-aware: the length of a string is its CODEPOINT count
   * (== byte count when the string is all ASCII) */
  if (MLuaStringIsAscii(MLuaGetStack(L, 1))) {
    MLuaPush(L, MakeInt((I32)len));
    return 1;
  }
  MLuaPush(L, MakeInt(s ? (I32)MLuaUTF8Len(s, len) : 0));
  return 1;
}

/* ========================================================================== */
/* string.lower                                                               */
/* ========================================================================== */

static int StringLower(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  char *buf;
  Size i;

  if (!s || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Heap scratch (no silent truncation); ASCII-only case mapping — full
   * Unicode case tables are out of budget for a tiny runtime. UTF-8
   * continuation bytes are > 0x7F and pass through untouched. */
  buf = (char *)MLuaAlloc(L, len);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return -1;
  }

  for (i = 0; i < len; i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') {
      buf[i] = c + 32;
    } else {
      buf[i] = c;
    }
  }

  return PushBuiltString(L, MLuaStringNew(L, buf, len));
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
    init = (Size)MLuaGetIntVal(vi);
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
  ms.CaptureDepth = 0;
  ms.OpenCapStart = NULL;
  ms.Error = NULL;

  /* Check for anchor */
  if (*pattern == '^') {
    const char *end = PatMatch(&ms, s + init - 1, pattern + 1);
    if (ms.Error) {
      L->ErrorMsg = ms.Error;
      return -1;
    }
    if (end) {
      if (ms.NumCaptures > 0) {
        /* Return captures */
        for (i = 0; i < (Size)ms.NumCaptures; i++) {
          MLuaValue cap = MLuaStringNew(L, ms.CapStarts[i],
                                        (Size)(ms.CapEnds[i] - ms.CapStarts[i]));
          if (IsNil(cap)) {
            return -1;
          }
          MLuaPush(L, cap);
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
    ms.CaptureDepth = 0;
    ms.OpenCapStart = NULL;
    ms.Error = NULL;
    const char *end = PatMatch(&ms, s + i, pattern);
    if (ms.Error) {
      L->ErrorMsg = ms.Error;
      return -1;
    }
    if (end) {
      if (ms.NumCaptures > 0) {
        /* Return captures */
        Size j;
        for (j = 0; j < (Size)ms.NumCaptures; j++) {
          MLuaValue cap = MLuaStringNew(L, ms.CapStarts[j],
                                        (Size)(ms.CapEnds[j] - ms.CapStarts[j]));
          if (IsNil(cap)) {
            return -1;
          }
          MLuaPush(L, cap);
        }
        return ms.NumCaptures;
      } else {
        /* Return whole match */
        return PushBuiltString(L, MLuaStringNew(L, s + i, (Size)(end - (s + i))));
      }
    }
  }

  MLuaPush(L, MLUA_NIL);
  return 1;
}

#if MLUA_ENABLE_PACK
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
      I8 val = (I8)MLuaGetIntVal(v);
      buf[pos++] = (char)val;
      break;
    }
    case 'B': { /* unsigned byte */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U8 val = (U8)MLuaGetIntVal(v);
      buf[pos++] = (char)val;
      break;
    }
    case 'h': { /* signed short */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      I16 val = (I16)MLuaGetIntVal(v);
      buf[pos++] = (char)(val & 0xFF);
      buf[pos++] = (char)((val >> 8) & 0xFF);
      break;
    }
    case 'H': { /* unsigned short */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U16 val = (U16)MLuaGetIntVal(v);
      buf[pos++] = (char)(val & 0xFF);
      buf[pos++] = (char)((val >> 8) & 0xFF);
      break;
    }
    case 'i':
    case 'I': { /* signed/unsigned int */
      MLuaValue v = MLuaGetStack(L, argIdx++);
      U32 val = (U32)MLuaGetIntVal(v);
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
      U64 val = IsInt(v) ? (U64)(U32)MLuaGetIntVal(v) : (U64)MLuaToNumber(v);
      int i;
      for (i = 0; i < 8; i++) {
        buf[pos++] = (char)((val >> (i * 8)) & 0xFF);
      }
      break;
    }
    case 'c': { /* fixed-size string: cn */
      Size n = 0;
      while (fi < fmtlen && fmt[fi] >= '0' && fmt[fi] <= '9') {
        if (n < sizeof(buf)) {
          n = n * 10 + (Size)(fmt[fi] - '0');
          if (n > sizeof(buf))
            n = sizeof(buf);
        }
        fi++;
      }
      if (n > 0) {
        Size slen;
        const char *s = GetStrArg(L, argIdx++, &slen);
        Size i;
        for (i = 0; i < n && pos < sizeof(buf); i++) {
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

  return PushBuiltString(L, MLuaStringNew(L, buf, pos));
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
      Size n = 0;
      while (fi < fmtlen && fmt[fi] >= '0' && fmt[fi] <= '9') {
        if (n <= (Size)-1 / 10) {
          Size next = n * 10 + (Size)(fmt[fi] - '0');
          n = next < n ? (Size)-1 : next;
        } else {
          n = (Size)-1;
        }
        fi++;
      }
      if (n > (Size)-1 - size) {
        MLuaPush(L, MLUA_NIL);
        MLuaPush(L, MLuaStringNew(L, "format too large", 16));
        return 2;
      }
      size += n;
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

#endif /* MLUA_ENABLE_PACK */

/* ========================================================================== */
/* string.rep                                                                 */
/* ========================================================================== */

static int StringRep(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  MLuaValue vn = MLuaGetStack(L, 2);
  I32 n = MLuaGetIntVal(vn);
  char *buf;
  Size total;
  Size bufpos = 0;
  I32 i;

  if (!s || n <= 0 || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Heap scratch sized exactly (no silent truncation); reclaimed at the
   * next collection */
  if ((Size)n > (Size)-1 / len) {
    L->ErrorMsg = "out of memory";
    return -1;
  }
  total = len * (Size)n;
  buf = (char *)MLuaAlloc(L, total);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return -1;
  }

  for (i = 0; i < n; i++) {
    MemCpy(buf + bufpos, s, len);
    bufpos += len;
  }

  return PushBuiltString(L, MLuaStringNew(L, buf, bufpos));
}

/* ========================================================================== */
/* string.reverse                                                             */
/* ========================================================================== */

static int StringReverse(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  char *buf;
  const char *p;
  const char *end;
  Size out;

  if (!s || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Unicode-aware: reverse the CODEPOINT sequence (multibyte sequences
   * stay intact). Transient heap scratch; reclaimed at the next collect. */
  buf = (char *)MLuaAlloc(L, len);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return -1;
  }

  p = s;
  end = s + len;
  out = len;
  while (p < end) {
    U32 cp;
    Size consumed = MLuaUTF8Decode(p, end, &cp);
    if (consumed == 0) {
      consumed = 1; /* Invalid byte: treat as a single unit */
    }
    out -= consumed;
    MemCpy(buf + out, p, consumed);
    p += consumed;
  }

  return PushBuiltString(L, MLuaStringNew(L, buf + out, len - out));
}

/* ========================================================================== */
/* string.sub                                                                 */
/* ========================================================================== */

static int StringSub(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  Bool ascii = MLuaStringIsAscii(MLuaGetStack(L, 1));
  IPtr i = 1;
  IPtr j = -1;
  IPtr total;
  Size byteStart;
  Size byteEnd;

  if (MLuaGetTop(L) >= 2) {
    MLuaValue vi = MLuaGetStack(L, 2);
    i = MLuaGetIntVal(vi);
  }
  if (MLuaGetTop(L) >= 3) {
    MLuaValue vj = MLuaGetStack(L, 3);
    j = MLuaGetIntVal(vj);
  }

  if (!s) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Unicode-aware: i and j are CODEPOINT indices (== byte indices when
   * the string is all ASCII, skipping the per-call decode) */
  total = ascii ? (IPtr)len : (IPtr)MLuaUTF8Len(s, len);

  /* Handle negative indices */
  if (i < 0)
    i = total + i + 1;
  if (j < 0)
    j = total + j + 1;

  /* Clamp */
  if (i < 1)
    i = 1;
  if (j > total)
    j = total;

  if (i > j) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
  } else {
    if (ascii) {
      byteStart = (Size)(i - 1);
      byteEnd = (Size)j;
    } else {
      byteStart = MLuaUTF8Offset(s, len, (Size)(i - 1));
      byteEnd = MLuaUTF8Offset(s, len, (Size)j);
    }
    return PushBuiltString(L, MLuaStringNew(L, s + byteStart, byteEnd - byteStart));
  }
  return 1;
}

#if MLUA_ENABLE_PACK
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
    pos = (Size)MLuaGetIntVal(vpos);
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
      /* Shift in U32: (U8)x << 24 would promote to int, which may be
       * narrower than 32 bits on some targets. */
      U32 val = (U32)(U8)data[pos] | ((U32)(U8)data[pos + 1] << 8) |
                ((U32)(U8)data[pos + 2] << 16) | ((U32)(U8)data[pos + 3] << 24);
      pos += 4;
      MLuaPush(L, MLuaMakeInt(L, (I32)val));
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
        MLuaPush(L, MLuaMakeInt(L, (I32)val));
      } else {
        MLuaPush(L, MLuaMakeNumber(L, (double)val));
      }
      count++;
      break;
    }
    case 'c': { /* fixed-size string: cn */
      Size n = 0;
      while (fi < fmtlen && fmt[fi] >= '0' && fmt[fi] <= '9') {
        if (n <= (Size)-1 / 10) {
          Size next = n * 10 + (Size)(fmt[fi] - '0');
          n = next < n ? (Size)-1 : next;
        } else {
          n = (Size)-1;
        }
        fi++;
      }
      if (n > 0 && n <= datalen - pos) {
        MLuaValue str = MLuaStringNew(L, data + pos, n);
        if (IsNil(str)) {
          return -1;
        }
        MLuaPush(L, str);
        pos += n;
        count++;
      }
      break;
    }
    case 'z': { /* null-terminated string */
      Size start = pos;
      while (pos < datalen && data[pos] != '\0')
        pos++;
      MLuaValue str = MLuaStringNew(L, data + start, pos - start);
      if (IsNil(str)) {
        return -1;
      }
      MLuaPush(L, str);
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

#endif /* MLUA_ENABLE_PACK */

/* ========================================================================== */
/* string.upper                                                               */
/* ========================================================================== */

static int StringUpper(MLuaState *L) {
  Size len;
  const char *s = GetStrArg(L, 1, &len);
  char *buf;
  Size i;

  if (!s || len == 0) {
    MLuaPush(L, MLuaStringNew(L, "", 0));
    return 1;
  }

  /* Heap scratch (no silent truncation); ASCII-only case mapping — full
   * Unicode case tables are out of budget for a tiny runtime. UTF-8
   * continuation bytes are > 0x7F and pass through untouched. */
  buf = (char *)MLuaAlloc(L, len);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return -1;
  }

  for (i = 0; i < len; i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') {
      buf[i] = c - 32;
    } else {
      buf[i] = c;
    }
  }

  return PushBuiltString(L, MLuaStringNew(L, buf, len));
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

static const MLuaLibEntry StringLibEntries[] = {
    {"byte", StringByte},     {"char", StringChar},
#if MLUA_ENABLE_DUMP
    {"dump", StringDump},
#endif
    {"find", StringFind},
    {"format", StringFormat}, {"len", StringLen},
    {"lower", StringLower},   {"match", StringMatch},
#if MLUA_ENABLE_PACK
    {"pack", StringPack},     {"packsize", StringPacksize},
#endif
    {"rep", StringRep},       {"reverse", StringReverse},
    {"sub", StringSub},
#if MLUA_ENABLE_PACK
    {"unpack", StringUnpack},
#endif
    {"upper", StringUpper},   {NULL, NULL}};

void MLuaOpenString(MLuaState *L) {
  MLuaValue lib = MLuaNewLib(L, "string");
  MLuaRegisterLib(L, lib, StringLibEntries);
}
