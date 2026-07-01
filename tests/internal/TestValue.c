/*
 * MicroLua - TestValue.c
 * Tests for MLuaValue.c (tagged pointer representation)
 */

#include "MLuaCore.h"
#include "MLuaValue.h"
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

/* ========================================================================== */
/* Special Values Tests                                                       */
/* ========================================================================== */

TEST(Nil) {
  MLuaValue v = MLUA_NIL;
  ASSERT(IsNil(v));
  ASSERT(!IsBool(v));
  ASSERT(!IsInt(v));
  ASSERT(IsFalsy(v));
  ASSERT(!IsTruthy(v));
}

TEST(True) {
  MLuaValue v = MLUA_TRUE;
  ASSERT(!IsNil(v));
  ASSERT(IsBool(v));
  ASSERT(IsTrue(v));
  ASSERT(!IsFalse(v));
  ASSERT(IsTruthy(v));
  ASSERT(!IsFalsy(v));
}

TEST(False) {
  MLuaValue v = MLUA_FALSE;
  ASSERT(!IsNil(v));
  ASSERT(IsBool(v));
  ASSERT(!IsTrue(v));
  ASSERT(IsFalse(v));
  ASSERT(!IsTruthy(v));
  ASSERT(IsFalsy(v));
}

TEST(BoolMacro) {
  ASSERT_EQ(MLUA_BOOL(1), MLUA_TRUE);
  ASSERT_EQ(MLUA_BOOL(0), MLUA_FALSE);
  ASSERT_EQ(MLUA_BOOL(42), MLUA_TRUE);
}

/* ========================================================================== */
/* Integer Tests                                                              */
/* ========================================================================== */

TEST(Integer_Zero) {
  MLuaValue v = MakeInt(0);
  ASSERT(IsInt(v));
  ASSERT_EQ(GetInt(v), 0);
  ASSERT(IsTruthy(v)); /* 0 is truthy in Lua! */
}

TEST(Integer_Positive) {
  MLuaValue v = MakeInt(42);
  ASSERT(IsInt(v));
  ASSERT_EQ(GetInt(v), 42);
}

TEST(Integer_Negative) {
  MLuaValue v = MakeInt(-100);
  ASSERT(IsInt(v));
  ASSERT_EQ(GetInt(v), -100);
}

TEST(Integer_MaxPositive) {
  /* Largest value that fits inline: the whole I32 range on the NaN-boxing path,
   * the 29-bit range on the tagging path. Wider values are boxed (see the
   * bigint interpreter tests), not stored by the raw inline MakeInt macro. */
  MLuaValue v = MakeInt(MLUA_INLINE_INT_MAX);
  ASSERT(IsInt(v));
  ASSERT_EQ(GetInt(v), MLUA_INLINE_INT_MAX);
}

TEST(Integer_MinNegative) {
  MLuaValue v = MakeInt(MLUA_INLINE_INT_MIN);
  ASSERT(IsInt(v));
  ASSERT_EQ(GetInt(v), MLUA_INLINE_INT_MIN);
}

TEST(Integer_Fits) {
  ASSERT(MLuaIntFits(0));
  ASSERT(MLuaIntFits(100));
  ASSERT(MLuaIntFits(-100));
  ASSERT(MLuaIntFits(MLUA_INLINE_INT_MAX));
  ASSERT(MLuaIntFits(MLUA_INLINE_INT_MIN));
#if MLUA_PTR_SIZE != 8
  /* On the tagging path the inline range is narrower than I32; values past it
   * do not fit inline (MLuaMakeIntSafe boxes them instead). */
  ASSERT(!MLuaIntFits(MLUA_INLINE_INT_MAX + 1));
  ASSERT(!MLuaIntFits(MLUA_INLINE_INT_MIN - 1));
  ASSERT(!MLuaIntFits(MLUA_INT_MAX));
  ASSERT(!MLuaIntFits(MLUA_INT_MIN));
#else
  /* On the NaN-boxing path the inline range is the whole I32 range. */
  ASSERT(MLuaIntFits(MLUA_INT_MAX));
  ASSERT(MLuaIntFits(MLUA_INT_MIN));
#endif
}

/* ========================================================================== */
/* Short String Tests                                                         */
/* ========================================================================== */

TEST(ShortStr_3Chars) {
  MLuaValue v = MakeShortStr('A', 'B', 'C');
  ASSERT(IsShortStr(v));
  ASSERT_EQ(GetShortStrChar0(v), 'A');
  ASSERT_EQ(GetShortStrChar1(v), 'B');
  ASSERT_EQ(GetShortStrChar2(v), 'C');
  ASSERT(IsTruthy(v));
}

TEST(ShortStr_Empty) {
  MLuaValue v = MakeShortStr(0, 0, 0);
  ASSERT(IsShortStr(v));
  ASSERT_EQ(MLuaShortStrLen(v), 0);
}

TEST(ShortStr_Length) {
  MLuaValue v1 = MakeShortStr('a', 0, 0);
  MLuaValue v2 = MakeShortStr('a', 'b', 0);
  MLuaValue v3 = MakeShortStr('a', 'b', 'c');

  ASSERT_EQ(MLuaShortStrLen(v1), 1);
  ASSERT_EQ(MLuaShortStrLen(v2), 2);
  ASSERT_EQ(MLuaShortStrLen(v3), 3);
#if MLUA_SHORTSTR_MAX >= 5
  {
    MLuaValue v5 = MLuaMakeShortStr("hello", 5);
    ASSERT_EQ(MLuaShortStrLen(v5), MLUA_SHORTSTR_MAX);
  }
#endif
}

TEST(ShortStr_FromCStr) {
  MLuaValue v = MLuaMakeShortStr("Hi", 2);
  ASSERT(IsShortStr(v));
  ASSERT_EQ(GetShortStrChar0(v), 'H');
  ASSERT_EQ(GetShortStrChar1(v), 'i');
  ASSERT_EQ(GetShortStrChar2(v), 0);
}

TEST(ShortStr_TooLong) {
  MLuaValue v = MLuaMakeShortStr("Hello!", 6);
  ASSERT(IsNil(v)); /* Should fail and return nil */
}

/* ========================================================================== */
/* Light Function Tests                                                       */
/* ========================================================================== */

TEST(LightFunc) {
  MLuaValue v = MakeLightFunc(42);
  ASSERT(IsLightFunc(v));
  ASSERT_EQ(GetLightFuncIdx(v), 42);
  ASSERT(IsTruthy(v));
}

/* ========================================================================== */
/* Pointer Tests                                                              */
/* ========================================================================== */

TEST(Pointer) {
  /* Use a statically allocated aligned object */
  static U64 aligned_obj MLUA_ALIGNAS(MLUA_ALIGNMENT) = 0xDEADBEEF;
  void *ptr = &aligned_obj;

  /* Verify the pointer is actually 8-byte aligned */
  ASSERT(((UPtr)ptr & TAG_MASK) == 0);

  MLuaValue v = MakePtr(ptr);
  ASSERT(IsPtr(v));
  ASSERT_EQ(GetPtr(v), ptr);
  ASSERT(IsTruthy(v));
}

/* ========================================================================== */
/* Tag Isolation Tests                                                        */
/* ========================================================================== */

TEST(TagIsolation) {
  /* Verify different tags produce different values */
  MLuaValue nil = MLUA_NIL;
  MLuaValue zero = MakeInt(0);
  MLuaValue lf = MakeLightFunc(0);
  MLuaValue ss = MakeShortStr(0, 0, 0);

  ASSERT_NE(nil, zero);
  ASSERT_NE(nil, lf);
  ASSERT_NE(nil, ss);
  ASSERT_NE(zero, lf);
  ASSERT_NE(zero, ss);
  ASSERT_NE(lf, ss);
}

/* ========================================================================== */
/* Raw Equality Tests                                                         */
/* ========================================================================== */

TEST(RawEquality) {
  ASSERT(MLuaRawEqual(MLUA_NIL, MLUA_NIL));
  ASSERT(MLuaRawEqual(MLUA_TRUE, MLUA_TRUE));
  ASSERT(MLuaRawEqual(MakeInt(42), MakeInt(42)));
  ASSERT(!MLuaRawEqual(MLUA_NIL, MLUA_FALSE));
  ASSERT(!MLuaRawEqual(MakeInt(1), MakeInt(2)));
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Value Tests\n");
  printf("====================\n\n");

  printf("Special Values:\n");
  RUN_TEST(Nil);
  RUN_TEST(True);
  RUN_TEST(False);
  RUN_TEST(BoolMacro);

  printf("\nIntegers:\n");
  RUN_TEST(Integer_Zero);
  RUN_TEST(Integer_Positive);
  RUN_TEST(Integer_Negative);
  RUN_TEST(Integer_MaxPositive);
  RUN_TEST(Integer_MinNegative);
  RUN_TEST(Integer_Fits);

  printf("\nShort Strings:\n");
  RUN_TEST(ShortStr_3Chars);
  RUN_TEST(ShortStr_Empty);
  RUN_TEST(ShortStr_Length);
  RUN_TEST(ShortStr_FromCStr);
  RUN_TEST(ShortStr_TooLong);

  printf("\nLight Functions:\n");
  RUN_TEST(LightFunc);

  printf("\nPointers:\n");
  RUN_TEST(Pointer);

  printf("\nTag Isolation:\n");
  RUN_TEST(TagIsolation);

  printf("\nRaw Equality:\n");
  RUN_TEST(RawEquality);

  printf("\n====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
