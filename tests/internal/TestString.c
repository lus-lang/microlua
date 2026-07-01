/*
 * MicroLua - TestString.c
 * Tests for MLuaString.c (interned strings)
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaString.h"
#include <stdio.h>

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
#define ASSERT_STREQ(a, b) ASSERT(StrCmp((a), (b)) == 0)

#define TEST_HEAP_SIZE (64 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

/* ========================================================================== */
/* Short String Tests                                                         */
/* ========================================================================== */

TEST(ShortStr_Create) {
  MLuaValue v = MLuaStringNewShort("Hi", 2);
  ASSERT(IsShortStr(v));
  ASSERT_EQ(MLuaStringLen(v), 2);
}

TEST(ShortStr_Empty) {
  MLuaValue v = MLuaStringNewShort("", 0);
  ASSERT(IsShortStr(v));
  ASSERT_EQ(MLuaStringLen(v), 0);
}

TEST(ShortStr_Data) {
  MLuaValue v = MLuaStringNewShort("abcde", 5);
  const char *s = MLuaStringData(v);
  ASSERT_STREQ(s, "abcde");
}

TEST(ShortStr_TooLong) {
  MLuaValue v = MLuaStringNewShort("hello!", 6);
  ASSERT(IsNil(v));
}

/* ========================================================================== */
/* Long String Tests                                                          */
/* ========================================================================== */

TEST(LongStr_Create) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v = MLuaStringNew(L, "Hello, World!", 13);
  ASSERT(IsString(v));
  ASSERT_EQ(MLuaStringLen(v), 13);
}

TEST(LongStr_Data) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v = MLuaStringNew(L, "Testing", 7);
  const char *s = MLuaStringData(v);
  ASSERT_STREQ(s, "Testing");
}

/*
 * len is always exact: len==0 creates the empty string. The old "0 means
 * auto-detect" contract was removed because it made empty strings from
 * non-NUL-terminated buffers impossible (and read out of bounds).
 */
TEST(LongStr_EmptyAndExplicitLen) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v = MLuaStringNew(L, "Hello", StrLen("Hello"));
  ASSERT_EQ(MLuaStringLen(v), 5);

  MLuaValue empty = MLuaStringNew(L, "garbage-not-read", 0);
  ASSERT_EQ(MLuaStringLen(empty), 0);
}

/* ========================================================================== */
/* Interning Tests                                                            */
/* ========================================================================== */

TEST(Intern_SameString) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v1 = MLuaStringNew(L, "interned", 8);
  MLuaValue v2 = MLuaStringNew(L, "interned", 8);

  /* Same string should return same value (interned) */
  ASSERT_EQ(v1, v2);
}

TEST(Intern_DifferentStrings) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v1 = MLuaStringNew(L, "hello", 5);
  MLuaValue v2 = MLuaStringNew(L, "world", 5);

  ASSERT(IsShortStr(v1));
  ASSERT(IsShortStr(v2));
  ASSERT_NE(v1, v2);
}

/* ========================================================================== */
/* String Comparison Tests                                                    */
/* ========================================================================== */

TEST(Compare_Equal) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v1 = MLuaStringNew(L, "test", 4);
  MLuaValue v2 = MLuaStringNew(L, "test", 4);

  ASSERT(MLuaStringEqual(v1, v2));
  ASSERT_EQ(MLuaStringCompare(v1, v2), 0);
}

TEST(Compare_Less) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v1 = MLuaStringNew(L, "apple", 5);
  MLuaValue v2 = MLuaStringNew(L, "banana", 6);

  ASSERT(MLuaStringCompare(v1, v2) < 0);
}

TEST(Compare_ShortVsLong) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue short_s = MLuaStringNewShort("ab", 2);
  MLuaValue long_s = MLuaStringNew(L, "abcdefgh", 8);

  ASSERT(MLuaStringCompare(short_s, long_s) < 0);
}

/* ========================================================================== */
/* Concatenation Tests                                                        */
/* ========================================================================== */

TEST(Concat_Basic) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v1 = MLuaStringNew(L, "Hello", 5);
  MLuaValue v2 = MLuaStringNew(L, " World", 6);
  MLuaValue result = MLuaStringConcat(L, v1, v2);

  ASSERT_EQ(MLuaStringLen(result), 11);
  ASSERT_STREQ(MLuaStringData(result), "Hello World");
}

TEST(Concat_Short) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue v1 = MLuaStringNewShort("a", 1);
  MLuaValue v2 = MLuaStringNewShort("b", 1);
  MLuaValue result = MLuaStringConcat(L, v1, v2);

  /* Result is "ab" which fits in short string */
  ASSERT_EQ(MLuaStringLen(result), 2);
}

/* ========================================================================== */
/* Hash Tests                                                                 */
/* ========================================================================== */

TEST(Hash_Consistency) {
  U32 h1 = MLuaStringHash("hello", 5);
  U32 h2 = MLuaStringHash("hello", 5);
  ASSERT_EQ(h1, h2);
}

TEST(Hash_Different) {
  U32 h1 = MLuaStringHash("hello", 5);
  U32 h2 = MLuaStringHash("world", 5);
  ASSERT_NE(h1, h2);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua String Tests\n");
  printf("=====================\n\n");

  printf("Short Strings:\n");
  RUN_TEST(ShortStr_Create);
  RUN_TEST(ShortStr_Empty);
  RUN_TEST(ShortStr_Data);
  RUN_TEST(ShortStr_TooLong);

  printf("\nLong Strings:\n");
  RUN_TEST(LongStr_Create);
  RUN_TEST(LongStr_Data);
  RUN_TEST(LongStr_EmptyAndExplicitLen);

  printf("\nInterning:\n");
  RUN_TEST(Intern_SameString);
  RUN_TEST(Intern_DifferentStrings);

  printf("\nComparison:\n");
  RUN_TEST(Compare_Equal);
  RUN_TEST(Compare_Less);
  RUN_TEST(Compare_ShortVsLong);

  printf("\nConcatenation:\n");
  RUN_TEST(Concat_Basic);
  RUN_TEST(Concat_Short);

  printf("\nHashing:\n");
  RUN_TEST(Hash_Consistency);
  RUN_TEST(Hash_Different);

  printf("\n=====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
