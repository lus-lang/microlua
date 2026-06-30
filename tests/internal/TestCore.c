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
