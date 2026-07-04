/*
 * MicroLua - TestCore.c
 * Tests for MLuaCore.c (memory and string functions)
 */

#include "MLuaCore.h"
#include <stdio.h>
#include <string.h>

static int TestsPassed = 0;
static int TestsFailed = 0;

#define TEST(name) static void Test_##name(void)
#define RUN_TEST(name)                                                         \
  do {                                                                         \
    printf("  Testing %s... ", #name);                                         \
    Test_##name();                                                             \
    printf("OK\n");                                                            \
    TestsPassed++;                                                             \
  } while (0)

#define ASSERT(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      printf("FAILED\n    Assertion failed: %s\n", #expr);                     \
      TestsFailed++;                                                           \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))

/* ========================================================================== */
/* Memory Function Tests                                                      */
/* ========================================================================== */

TEST(MemCpy_Basic) {
  char src[] = "Hello, World!";
  char dst[20] = {0};
  MemCpy(dst, src, 14);
  ASSERT_EQ(strcmp(dst, "Hello, World!"), 0);
}

TEST(MemCpy_Empty) {
  char src[] = "test";
  char dst[10] = "original";
  MemCpy(dst, src, 0);
  ASSERT_EQ(strcmp(dst, "original"), 0);
}

TEST(MemSet_Basic) {
  char buf[10];
  MemSet(buf, 'X', 5);
  buf[5] = '\0';
  ASSERT_EQ(strcmp(buf, "XXXXX"), 0);
}

TEST(MemSet_Zero) {
  char buf[10] = "hello";
  MemSet(buf, 0, 5);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(buf[i], 0);
  }
}

TEST(MemMove_NoOverlap) {
  char buf[20] = "Hello, World!";
  char dst[20] = {0};
  MemMove(dst, buf, 14);
  ASSERT_EQ(strcmp(dst, "Hello, World!"), 0);
}

TEST(MemMove_OverlapForward) {
  char buf[20] = "0123456789";
  MemMove(buf + 2, buf, 8);
  ASSERT_EQ(MemCmp(buf + 2, "01234567", 8), 0);
}

TEST(MemMove_OverlapBackward) {
  char buf[20] = "0123456789";
  MemMove(buf, buf + 2, 8);
  ASSERT_EQ(MemCmp(buf, "23456789", 8), 0);
}

/*
 * Alignment/overlap matrix for the word-wise Mem* bodies: every (dst
 * offset, src offset, length) combination around the word size, against
 * reference results, with guard bytes fencing both ends of the window.
 * Exercises co-aligned and misaligned pairs, head/body/tail splits, and
 * both MemMove directions at overlap distances around one word.
 */
#define MATRIX_WORD ((int)sizeof(void *))
#define MATRIX_SPAN (6 * MATRIX_WORD)
#define GUARD 4

TEST(MemCpy_Matrix) {
  unsigned char src[MATRIX_SPAN + 2 * GUARD];
  unsigned char dst[MATRIX_SPAN + 2 * GUARD];
  int dOff, sOff, len, i;

  for (dOff = 0; dOff <= 2 * MATRIX_WORD + 2; dOff++) {
    for (sOff = 0; sOff <= 2 * MATRIX_WORD + 2; sOff++) {
      for (len = 0; len <= 3 * MATRIX_WORD + 2; len++) {
        for (i = 0; i < MATRIX_SPAN + 2 * GUARD; i++) {
          src[i] = (unsigned char)(0x40 + (i & 0x3F));
          dst[i] = 0xAA;
        }
        MemCpy(dst + GUARD + dOff, src + GUARD + sOff, (Size)len);
        for (i = 0; i < len; i++) {
          ASSERT_EQ(dst[GUARD + dOff + i], src[GUARD + sOff + i]);
        }
        for (i = 0; i < GUARD + dOff; i++) {
          ASSERT_EQ(dst[i], 0xAA);
        }
        for (i = GUARD + dOff + len; i < MATRIX_SPAN + 2 * GUARD; i++) {
          ASSERT_EQ(dst[i], 0xAA);
        }
      }
    }
  }
}

TEST(MemSet_Matrix) {
  unsigned char dst[MATRIX_SPAN + 2 * GUARD];
  int dOff, len, i;

  for (dOff = 0; dOff <= 2 * MATRIX_WORD + 2; dOff++) {
    for (len = 0; len <= 3 * MATRIX_WORD + 2; len++) {
      for (i = 0; i < MATRIX_SPAN + 2 * GUARD; i++) {
        dst[i] = 0xAA;
      }
      MemSet(dst + GUARD + dOff, 0x5C, (Size)len);
      for (i = 0; i < GUARD + dOff; i++) {
        ASSERT_EQ(dst[i], 0xAA);
      }
      for (i = 0; i < len; i++) {
        ASSERT_EQ(dst[GUARD + dOff + i], 0x5C);
      }
      for (i = GUARD + dOff + len; i < MATRIX_SPAN + 2 * GUARD; i++) {
        ASSERT_EQ(dst[i], 0xAA);
      }
    }
  }
}

TEST(MemMove_OverlapMatrix) {
  unsigned char buf[MATRIX_SPAN + 2 * GUARD];
  unsigned char ref[MATRIX_SPAN + 2 * GUARD];
  int base, delta, len, i;

  /* delta spans both directions through +/- (word+1); base walks the
   * window across word boundaries so every alignment pairing occurs */
  for (base = 0; base <= MATRIX_WORD; base++) {
    for (delta = -(MATRIX_WORD + 1); delta <= MATRIX_WORD + 1; delta++) {
      for (len = 0; len <= 3 * MATRIX_WORD + 2; len++) {
        int srcAt = GUARD + MATRIX_WORD + 1 + base;
        int dstAt = srcAt + delta;
        for (i = 0; i < MATRIX_SPAN + 2 * GUARD; i++) {
          buf[i] = (unsigned char)(0x40 + (i & 0x3F));
          ref[i] = buf[i];
        }
        /* Reference result via a disjoint scratch copy */
        {
          unsigned char scratch[MATRIX_SPAN + 2 * GUARD];
          for (i = 0; i < len; i++) {
            scratch[i] = ref[srcAt + i];
          }
          for (i = 0; i < len; i++) {
            ref[dstAt + i] = scratch[i];
          }
        }
        MemMove(buf + dstAt, buf + srcAt, (Size)len);
        for (i = 0; i < MATRIX_SPAN + 2 * GUARD; i++) {
          ASSERT_EQ(buf[i], ref[i]);
        }
      }
    }
  }
}

TEST(MemCmp_Equal) {
  char a[] = "hello";
  char b[] = "hello";
  ASSERT_EQ(MemCmp(a, b, 5), 0);
}

TEST(MemCmp_Less) {
  char a[] = "abc";
  char b[] = "abd";
  ASSERT(MemCmp(a, b, 3) < 0);
}

TEST(MemCmp_Greater) {
  char a[] = "abd";
  char b[] = "abc";
  ASSERT(MemCmp(a, b, 3) > 0);
}

/* ========================================================================== */
/* String Function Tests                                                      */
/* ========================================================================== */

TEST(StrLen_Basic) {
  ASSERT_EQ(StrLen("hello"), 5);
  ASSERT_EQ(StrLen(""), 0);
  ASSERT_EQ(StrLen("a"), 1);
}

TEST(StrCmp_Equal) {
  ASSERT_EQ(StrCmp("hello", "hello"), 0);
  ASSERT_EQ(StrCmp("", ""), 0);
}

TEST(StrCmp_Less) {
  ASSERT(StrCmp("abc", "abd") < 0);
  ASSERT(StrCmp("abc", "abcd") < 0);
}

TEST(StrCmp_Greater) {
  ASSERT(StrCmp("abd", "abc") > 0);
  ASSERT(StrCmp("abcd", "abc") > 0);
}

TEST(StrNCmp_Equal) {
  ASSERT_EQ(StrNCmp("hello", "hello", 5), 0);
  ASSERT_EQ(StrNCmp("hello", "help", 3), 0);
}

TEST(StrNCmp_Limit) { ASSERT_EQ(StrNCmp("hello", "helloworld", 5), 0); }

TEST(StrChr_Found) {
  const char *s = "hello";
  ASSERT_EQ(StrChr(s, 'e'), s + 1);
  ASSERT_EQ(StrChr(s, 'h'), s);
  ASSERT_EQ(StrChr(s, 'o'), s + 4);
}

TEST(StrChr_NotFound) {
  const char *s = "hello";
  ASSERT_EQ(StrChr(s, 'x'), NULL);
}

TEST(StrChr_Null) {
  const char *s = "hello";
  ASSERT_EQ(StrChr(s, '\0'), s + 5);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Core Tests\n");
  printf("===================\n\n");

  printf("Memory Functions:\n");
  RUN_TEST(MemCpy_Basic);
  RUN_TEST(MemCpy_Empty);
  RUN_TEST(MemSet_Basic);
  RUN_TEST(MemSet_Zero);
  RUN_TEST(MemMove_NoOverlap);
  RUN_TEST(MemMove_OverlapForward);
  RUN_TEST(MemMove_OverlapBackward);
  RUN_TEST(MemCpy_Matrix);
  RUN_TEST(MemSet_Matrix);
  RUN_TEST(MemMove_OverlapMatrix);
  RUN_TEST(MemCmp_Equal);
  RUN_TEST(MemCmp_Less);
  RUN_TEST(MemCmp_Greater);

  printf("\nString Functions:\n");
  RUN_TEST(StrLen_Basic);
  RUN_TEST(StrCmp_Equal);
  RUN_TEST(StrCmp_Less);
  RUN_TEST(StrCmp_Greater);
  RUN_TEST(StrNCmp_Equal);
  RUN_TEST(StrNCmp_Limit);
  RUN_TEST(StrChr_Found);
  RUN_TEST(StrChr_NotFound);
  RUN_TEST(StrChr_Null);

  printf("\n===================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
