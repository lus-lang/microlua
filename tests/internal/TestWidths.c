/*
 * MicroLua - TestWidths.c
 * Fixed-width and pointer-width type invariants. Prints the detected widths so
 * the "nonstandard target" property is visible when this runs under a cross or
 * -m32 build-dir, not just asserted.
 */

#include "MLuaCore.h"
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

/* ========================================================================== */
/* Fixed-width Type Sizes                                                      */
/* ========================================================================== */

TEST(IntegerWidths) {
  ASSERT(sizeof(U8) == 1);
  ASSERT(sizeof(I8) == 1);
  ASSERT(sizeof(U16) == 2);
  ASSERT(sizeof(I16) == 2);
  ASSERT(sizeof(U32) == 4);
  ASSERT(sizeof(I32) == 4);
  ASSERT(sizeof(U64) == 8);
  ASSERT(sizeof(I64) == 8);
}

TEST(Signedness) {
  ASSERT((I8)-1 < 0);
  ASSERT((I16)-1 < 0);
  ASSERT((I32)-1 < 0);
  ASSERT((U8)0xFFu > 0);
  ASSERT((U16)0xFFFFu > 0);
  ASSERT((U32)0xFFFFFFFFu > 0);
}

/* ========================================================================== */
/* Pointer / Value Word Width                                                  */
/* ========================================================================== */

TEST(PointerWidth) {
  /* UPtr (and therefore MLuaValue) must be able to hold a pointer. */
  ASSERT(sizeof(UPtr) >= sizeof(void *));
  ASSERT(sizeof(Size) == sizeof(UPtr));
}

TEST(RepresentationSelector) {
  /* MLUA_PTR_SIZE selects the value representation; 8 => NaN-boxing needs an
   * 8-byte word, otherwise the tagging path needs at least a 32-bit word. */
#if MLUA_PTR_SIZE == 8
  ASSERT(sizeof(UPtr) == 8);
#else
  ASSERT(sizeof(UPtr) >= 4);
#endif
}

int main(void) {
  printf("Running TestWidths...\n");
  printf("  detected: int=%d ptr=%d U32=%d U64=%d UPtr=%d MLUA_PTR_SIZE=%d\n",
         (int)sizeof(int), (int)sizeof(void *), (int)sizeof(U32),
         (int)sizeof(U64), (int)sizeof(UPtr), (int)MLUA_PTR_SIZE);

  printf("\nFixed-width sizes:\n");
  RUN_TEST(IntegerWidths);
  RUN_TEST(Signedness);

  printf("\nPointer / value word:\n");
  RUN_TEST(PointerWidth);
  RUN_TEST(RepresentationSelector);

  printf("\n====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
