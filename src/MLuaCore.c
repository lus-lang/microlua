/*
 * MicroLua - MLuaCore.c
 * Freestanding libc replacements
 */

#include "MLuaCore.h"

/* ========================================================================== */
/* Memory functions                                                           */
/* ========================================================================== */

void *MemCpy(void *dest, const void *src, Size n) {
  U8 *d = (U8 *)dest;
  const U8 *s = (const U8 *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

void *MemSet(void *dest, int val, Size n) {
  U8 *d = (U8 *)dest;
  U8 v = (U8)val;
  while (n--) {
    *d++ = v;
  }
  return dest;
}

void *MemMove(void *dest, const void *src, Size n) {
  U8 *d = (U8 *)dest;
  const U8 *s = (const U8 *)src;

  if (d == s || n == 0) {
    return dest;
  }

  /* If dest is before src, copy forward */
  if (d < s) {
    while (n--) {
      *d++ = *s++;
    }
  } else {
    /* Overlap: copy backward */
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }
  return dest;
}

int MemCmp(const void *s1, const void *s2, Size n) {
  const U8 *p1 = (const U8 *)s1;
  const U8 *p2 = (const U8 *)s2;

  while (n--) {
    if (*p1 != *p2) {
      return (*p1 < *p2) ? -1 : 1;
    }
    p1++;
    p2++;
  }
  return 0;
}

/* ========================================================================== */
/* String functions                                                           */
/* ========================================================================== */

Size StrLen(const char *s) {
  const char *p = s;
  while (*p) {
    p++;
  }
  return (Size)(p - s);
}

int StrCmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return (U8)*s1 - (U8)*s2;
}

int StrNCmp(const char *s1, const char *s2, Size n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0) {
    return 0;
  }
  return (U8)*s1 - (U8)*s2;
}

char *StrChr(const char *s, int c) {
  char ch = (char)c;
  while (*s) {
    if (*s == ch) {
      return (char *)s;
    }
    s++;
  }
  return (c == '\0') ? (char *)s : NULL;
}

/* ========================================================================== */
/* Debug (when MLUA_DEBUG is defined)                                         */
/* ========================================================================== */

#ifdef MLUA_DEBUG
/*
 * This would typically call a platform-specific handler.
 * For freestanding, this is a stub that loops forever.
 */
void MLuaAssertFail(const char *expr, const char *file, int line) {
  UNUSED(expr);
  UNUSED(file);
  UNUSED(line);
  /* Infinite loop - in embedded, this might trigger a watchdog reset */
  for (;;) {
  }
}
#endif
