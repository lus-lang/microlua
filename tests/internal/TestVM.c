/*
 * MicroLua - TestVM.c
 * Tests for MLuaVM.c (virtual machine)
 */

#include "MLuaAlloc.h"
#include "MLuaCode.h"
#include "MLuaCore.h"
#include "MLuaFunc.h"
#include "MLuaParse.h"
#include "MLuaVM.h"
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

#define TEST_HEAP_SIZE (256 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

/* ========================================================================== */
/* VM Stack Tests                                                             */
/* ========================================================================== */

TEST(StackPushPop) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);

  ASSERT_EQ(MLuaGetTop(L), 0);

  MLuaPush(L, MakeInt(42));
  ASSERT_EQ(MLuaGetTop(L), 1);

  MLuaPush(L, MakeInt(100));
  ASSERT_EQ(MLuaGetTop(L), 2);

  MLuaValue v = MLuaPop(L);
  ASSERT_EQ(GetInt(v), 100);
  ASSERT_EQ(MLuaGetTop(L), 1);

  v = MLuaPop(L);
  ASSERT_EQ(GetInt(v), 42);
  ASSERT_EQ(MLuaGetTop(L), 0);
}

TEST(StackIndexing) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);

  MLuaPush(L, MakeInt(10));
  MLuaPush(L, MakeInt(20));
  MLuaPush(L, MakeInt(30));

  /* Positive indexing (1-based) */
  ASSERT_EQ(GetInt(MLuaGetStack(L, 1)), 10);
  ASSERT_EQ(GetInt(MLuaGetStack(L, 2)), 20);
  ASSERT_EQ(GetInt(MLuaGetStack(L, 3)), 30);

  /* Negative indexing (from top) */
  ASSERT_EQ(GetInt(MLuaGetStack(L, -1)), 30);
  ASSERT_EQ(GetInt(MLuaGetStack(L, -2)), 20);
  ASSERT_EQ(GetInt(MLuaGetStack(L, -3)), 10);
}

/* ========================================================================== */
/* Arithmetic Tests                                                           */
/* ========================================================================== */

TEST(ArithAdd) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaValue result = MLuaArith(L, OP_ADD, MakeInt(10), MakeInt(32));
  ASSERT_EQ(GetInt(result), 42);
}

TEST(ArithSub) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaValue result = MLuaArith(L, OP_SUB, MakeInt(50), MakeInt(8));
  ASSERT_EQ(GetInt(result), 42);
}

TEST(ArithMul) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaValue result = MLuaArith(L, OP_MUL, MakeInt(6), MakeInt(7));
  ASSERT_EQ(GetInt(result), 42);
}

TEST(ArithNeg) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaValue result = MLuaArith(L, OP_UNM, MakeInt(42), MLUA_NIL);
  ASSERT_EQ(GetInt(result), -42);
}

/* ========================================================================== */
/* Comparison Tests                                                           */
/* ========================================================================== */

TEST(CompareEq) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT(MLuaCompare(L, OP_EQ, MakeInt(42), MakeInt(42)));
  ASSERT(!MLuaCompare(L, OP_EQ, MakeInt(42), MakeInt(43)));
}

TEST(CompareLt) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT(MLuaCompare(L, OP_LT, MakeInt(1), MakeInt(2)));
  ASSERT(!MLuaCompare(L, OP_LT, MakeInt(2), MakeInt(2)));
  ASSERT(!MLuaCompare(L, OP_LT, MakeInt(3), MakeInt(2)));
}

TEST(CompareLe) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT(MLuaCompare(L, OP_LE, MakeInt(1), MakeInt(2)));
  ASSERT(MLuaCompare(L, OP_LE, MakeInt(2), MakeInt(2)));
  ASSERT(!MLuaCompare(L, OP_LE, MakeInt(3), MakeInt(2)));
}

/* ========================================================================== */
/* DoString Tests                                                             */
/* ========================================================================== */

TEST(DoStringReturn) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return 42";

  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(DoStringSimple) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local x = 1 + 2";

  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua VM Tests\n");
  printf("=================\n\n");

  printf("Stack Operations:\n");
  RUN_TEST(StackPushPop);
  RUN_TEST(StackIndexing);

  printf("\nArithmetic:\n");
  RUN_TEST(ArithAdd);
  RUN_TEST(ArithSub);
  RUN_TEST(ArithMul);
  RUN_TEST(ArithNeg);

  printf("\nComparison:\n");
  RUN_TEST(CompareEq);
  RUN_TEST(CompareLt);
  RUN_TEST(CompareLe);

  printf("\nExecution:\n");
  RUN_TEST(DoStringReturn);
  RUN_TEST(DoStringSimple);

  printf("\n=================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
