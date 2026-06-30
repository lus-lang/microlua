/*
 * MicroLua - MLuaUTF8.c
 * UTF-8 / WTF-8 encoding utilities
 */

#include "MLuaUTF8.h"

/* ========================================================================== */
/* Decoding                                                                   */
/* ========================================================================== */

Size MLuaUTF8SeqLen(U8 leadByte) {
  if ((leadByte & 0x80) == 0x00)
    return 1; /* 0xxxxxxx */
  if ((leadByte & 0xE0) == 0xC0)
    return 2; /* 110xxxxx */
  if ((leadByte & 0xF0) == 0xE0)
    return 3; /* 1110xxxx */
  if ((leadByte & 0xF8) == 0xF0)
    return 4; /* 11110xxx */
  return 0;   /* Invalid lead byte */
}

Bool MLuaUTF8IsCont(U8 byte) { return (byte & 0xC0) == 0x80; /* 10xxxxxx */ }

Bool MLuaUTF8IsSurrogate(U32 cp) {
  return cp >= MLUA_SURROGATE_MIN && cp <= MLUA_SURROGATE_MAX;
}

Size MLuaUTF8Decode(const char *str, const char *end, U32 *cpOut) {
  U8 lead;
  Size seqLen;
  U32 cp;
  Size i;

  if (str >= end) {
    *cpOut = MLUA_UTF8_REPLACEMENT;
    return 0;
  }

  lead = (U8)*str;
  seqLen = MLuaUTF8SeqLen(lead);

  if (seqLen == 0 || str + seqLen > end) {
    *cpOut = MLUA_UTF8_REPLACEMENT;
    return 1; /* Skip one byte */
  }

  /* Decode based on sequence length */
  switch (seqLen) {
  case 1:
    cp = lead;
    break;

  case 2:
    cp = lead & 0x1F;
    break;

  case 3:
    cp = lead & 0x0F;
    break;

  case 4:
    cp = lead & 0x07;
    break;

  default:
    *cpOut = MLUA_UTF8_REPLACEMENT;
    return 1;
  }

  /* Process continuation bytes */
  for (i = 1; i < seqLen; i++) {
    U8 cont = (U8)str[i];
    if (!MLuaUTF8IsCont(cont)) {
      *cpOut = MLUA_UTF8_REPLACEMENT;
      return i; /* Return bytes consumed before error */
    }
    cp = (cp << 6) | (cont & 0x3F);
  }

  /* Check for overlong encodings */
  if ((seqLen == 2 && cp < 0x80) || (seqLen == 3 && cp < 0x800) ||
      (seqLen == 4 && cp < 0x10000)) {
    *cpOut = MLUA_UTF8_REPLACEMENT;
    return seqLen;
  }

  /* Check for out-of-range codepoints */
  if (cp > 0x10FFFF) {
    *cpOut = MLUA_UTF8_REPLACEMENT;
    return seqLen;
  }

  *cpOut = cp;
  return seqLen;
}

Bool MLuaUTF8Validate(const char *str, Size len) {
  const char *end = str + len;
  U32 cp;

  while (str < end) {
    Size consumed = MLuaUTF8Decode(str, end, &cp);
    if (consumed == 0 || cp == MLUA_UTF8_REPLACEMENT) {
      return FALSE;
    }
    /* Strict UTF-8 rejects surrogates */
    if (MLuaUTF8IsSurrogate(cp)) {
      return FALSE;
    }
    str += consumed;
  }

  return TRUE;
}

Bool MLuaWTF8Validate(const char *str, Size len) {
  const char *end = str + len;
  U32 cp;

  while (str < end) {
    Size consumed = MLuaUTF8Decode(str, end, &cp);
    if (consumed == 0 || cp == MLUA_UTF8_REPLACEMENT) {
      return FALSE;
    }
    /* WTF-8 allows surrogates, but not paired surrogates in sequence */
    /* (A high surrogate followed immediately by a low surrogate is invalid) */
    if (cp >= MLUA_HIGH_SURROGATE_MIN && cp <= MLUA_HIGH_SURROGATE_MAX) {
      const char *next = str + consumed;
      if (next < end) {
        U32 nextCp;
        Size nextLen = MLuaUTF8Decode(next, end, &nextCp);
        if (nextLen > 0 && nextCp >= MLUA_LOW_SURROGATE_MIN &&
            nextCp <= MLUA_LOW_SURROGATE_MAX) {
          /* Paired surrogate - invalid in WTF-8 */
          return FALSE;
        }
      }
    }
    str += consumed;
  }

  return TRUE;
}

/* ========================================================================== */
/* Encoding                                                                   */
/* ========================================================================== */

Size MLuaUTF8Encode(U32 cp, char *buf) {
  if (cp < 0x80) {
    buf[0] = (char)cp;
    return 1;
  }

  if (cp < 0x800) {
    buf[0] = (char)(0xC0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }

  if (cp < 0x10000) {
    buf[0] = (char)(0xE0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }

  if (cp <= 0x10FFFF) {
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }

  return 0; /* Invalid codepoint */
}

/* ========================================================================== */
/* String Operations                                                          */
/* ========================================================================== */

Size MLuaUTF8Len(const char *str, Size byteLen) {
  const char *end = str + byteLen;
  Size count = 0;
  U32 cp;

  while (str < end) {
    Size consumed = MLuaUTF8Decode(str, end, &cp);
    if (consumed == 0)
      break;
    count++;
    str += consumed;
  }

  return count;
}

Size MLuaUTF8Offset(const char *str, Size byteLen, Size cpIndex) {
  const char *end = str + byteLen;
  const char *p = str;
  Size i = 0;
  U32 cp;

  while (p < end && i < cpIndex) {
    Size consumed = MLuaUTF8Decode(p, end, &cp);
    if (consumed == 0)
      break;
    p += consumed;
    i++;
  }

  return (Size)(p - str);
}

const char *MLuaUTF8Next(const char *str, const char *end) {
  U32 cp;
  Size consumed;

  if (str >= end) {
    return end;
  }

  consumed = MLuaUTF8Decode(str, end, &cp);
  return str + (consumed > 0 ? consumed : 1);
}

const char *MLuaUTF8Prev(const char *str, const char *start) {
  if (str <= start) {
    return start;
  }

  /* Move back, skipping continuation bytes */
  str--;
  while (str > start && MLuaUTF8IsCont((U8)*str)) {
    str--;
  }

  return str;
}

/* ========================================================================== */
/* Character Classification                                                   */
/* ========================================================================== */

Bool MLuaUTF8IsDigit(U32 cp) { return cp >= '0' && cp <= '9'; }

Bool MLuaUTF8IsSpace(U32 cp) {
  /* ASCII whitespace */
  if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' ||
      cp == '\f') {
    return TRUE;
  }

  /* Unicode whitespace */
  switch (cp) {
  case 0x00A0: /* No-Break Space */
  case 0x1680: /* Ogham Space Mark */
  case 0x2000: /* En Quad */
  case 0x2001: /* Em Quad */
  case 0x2002: /* En Space */
  case 0x2003: /* Em Space */
  case 0x2004: /* Three-Per-Em Space */
  case 0x2005: /* Four-Per-Em Space */
  case 0x2006: /* Six-Per-Em Space */
  case 0x2007: /* Figure Space */
  case 0x2008: /* Punctuation Space */
  case 0x2009: /* Thin Space */
  case 0x200A: /* Hair Space */
  case 0x2028: /* Line Separator */
  case 0x2029: /* Paragraph Separator */
  case 0x202F: /* Narrow No-Break Space */
  case 0x205F: /* Medium Mathematical Space */
  case 0x3000: /* Ideographic Space */
    return TRUE;
  default:
    return FALSE;
  }
}

Bool MLuaUTF8IsAlpha(U32 cp) {
  /* ASCII letters and underscore */
  if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_') {
    return TRUE;
  }

  /* Extended Latin letters (common European languages) */
  if ((cp >= 0x00C0 && cp <= 0x00FF && cp != 0x00D7 && cp != 0x00F7)) {
    return TRUE; /* Latin Extended-A */
  }

  /* Latin Extended-A */
  if (cp >= 0x0100 && cp <= 0x017F) {
    return TRUE;
  }

  /* Latin Extended-B */
  if (cp >= 0x0180 && cp <= 0x024F) {
    return TRUE;
  }

  /* Greek */
  if (cp >= 0x0370 && cp <= 0x03FF) {
    return TRUE;
  }

  /* Cyrillic */
  if (cp >= 0x0400 && cp <= 0x04FF) {
    return TRUE;
  }

  /* CJK unified ideographs (subset - main block) */
  if (cp >= 0x4E00 && cp <= 0x9FFF) {
    return TRUE;
  }

  /* Hiragana */
  if (cp >= 0x3040 && cp <= 0x309F) {
    return TRUE;
  }

  /* Katakana */
  if (cp >= 0x30A0 && cp <= 0x30FF) {
    return TRUE;
  }

  /* Hangul syllables */
  if (cp >= 0xAC00 && cp <= 0xD7AF) {
    return TRUE;
  }

  return FALSE;
}

Bool MLuaUTF8IsAlnum(U32 cp) {
  return MLuaUTF8IsAlpha(cp) || MLuaUTF8IsDigit(cp);
}
