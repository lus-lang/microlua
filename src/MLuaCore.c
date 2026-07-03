/*
 * MicroLua - MLuaCore.c
 * Freestanding libc replacements
 */

#include "MLuaCore.h"

/* ========================================================================== */
/* Memory functions                                                           */
/* ========================================================================== */

#if !MLUA_PORT_MEMFUNCS

#if MLUA_MEM_WORDWISE
/* Word type for the block bodies below. may_alias licenses viewing byte
 * buffers through pointer-width loads/stores regardless of the buffer's
 * declared type; without it the casts would violate strict aliasing. */
typedef UPtr __attribute__((may_alias)) MemWord;
#define MEMWORD_SIZE ((Size)sizeof(MemWord))
#endif

void *MemCpy(void *dest, const void *src, Size n) {
  U8 *d = (U8 *)dest;
  const U8 *s = (const U8 *)src;
#if MLUA_MEM_WORDWISE
  /* Word body only when both pointers reach word alignment together; the
   * byte loop below then handles at most MEMWORD_SIZE-1 tail bytes. When
   * d < s this is also safe for overlapping buffers (MemMove's forward
   * path relies on that): each word is read before any write can reach it
   * in an ascending copy. */
  if (n >= 2 * MEMWORD_SIZE &&
      (((UPtr)d ^ (UPtr)s) & (MEMWORD_SIZE - 1)) == 0) {
    while (((UPtr)d & (MEMWORD_SIZE - 1)) != 0) {
      *d++ = *s++;
      n--;
    }
    while (n >= MEMWORD_SIZE) {
      *(MemWord *)(void *)d = *(const MemWord *)(const void *)s;
      d += MEMWORD_SIZE;
      s += MEMWORD_SIZE;
      n -= MEMWORD_SIZE;
    }
  }
#endif
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

void *MemSet(void *dest, int val, Size n) {
  U8 *d = (U8 *)dest;
  U8 v = (U8)val;
#if MLUA_MEM_WORDWISE
  if (n >= 2 * MEMWORD_SIZE) {
    /* Splat the byte across a word: UPTR_MAX/0xFF is 0x0101...01 */
    MemWord w = (MemWord)v * ((MemWord)-1 / 0xFF);
    while (((UPtr)d & (MEMWORD_SIZE - 1)) != 0) {
      *d++ = v;
      n--;
    }
    while (n >= MEMWORD_SIZE) {
      *(MemWord *)(void *)d = w;
      d += MEMWORD_SIZE;
      n -= MEMWORD_SIZE;
    }
  }
#endif
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

  /* Dest below src: an ascending copy is overlap-safe, and MemCpy's word
   * body preserves that (reads stay ahead of writes; see the note there).
   * This is the direction GC compaction always takes. */
  if (d < s) {
    return MemCpy(dest, src, n);
  }

  /* Dest above src: copy backward. The descending word body is the mirror
   * image -- reads stay below the write frontier for any positive offset. */
  d += n;
  s += n;
#if MLUA_MEM_WORDWISE
  if (n >= 2 * MEMWORD_SIZE &&
      (((UPtr)d ^ (UPtr)s) & (MEMWORD_SIZE - 1)) == 0) {
    while (((UPtr)d & (MEMWORD_SIZE - 1)) != 0) {
      *--d = *--s;
      n--;
    }
    while (n >= MEMWORD_SIZE) {
      d -= MEMWORD_SIZE;
      s -= MEMWORD_SIZE;
      *(MemWord *)(void *)d = *(const MemWord *)(const void *)s;
      n -= MEMWORD_SIZE;
    }
  }
#endif
  while (n--) {
    *--d = *--s;
  }
  return dest;
}

#endif /* !MLUA_PORT_MEMFUNCS */

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
