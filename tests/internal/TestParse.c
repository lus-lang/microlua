/*
 * MicroLua - TestParse.c
 * Tests for MLuaParse.c (parser and code generation)
 */

#include "MLuaAlloc.h"
#include "MLuaCode.h"
#include "MLuaCore.h"
#include "MLuaParse.h"
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

#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_EQ(a, b) ASSERT((a) == (b))

#define TEST_HEAP_SIZE (128 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

/* ========================================================================== */
/* Parse Tests                                                                */
/* ========================================================================== */

/* Only meaningful when MLuaIdx is narrower than Size (e.g. -DMLUA_IDX_T=U16
 * variant builds): a chunk whose bytecode outgrows MLUA_IDX_MAX must be
 * rejected with "function too large", never silently truncated. */
static U8 BigFnHeap[512 * 1024] MLUA_ALIGNAS(MLUA_ALIGNMENT);
static char BigFnSource[600 * 1024];

TEST(FunctionTooLarge) {
  MLuaState *L;
  MLuaProto *proto;
  Size pos = 0;
  static const char stmt[] = "x=x+1 "; /* one line: line map stays tiny */

  if (sizeof(MLuaIdx) >= sizeof(Size)) {
    return; /* limit unreachable in this configuration */
  }

  L = MLuaStateInit(BigFnHeap, sizeof(BigFnHeap));
  ASSERT_NE(L, NULL);

  while (pos + sizeof(stmt) < sizeof(BigFnSource) - 1) {
    MemCpy(BigFnSource + pos, stmt, sizeof(stmt) - 1);
    pos += sizeof(stmt) - 1;
  }
  BigFnSource[pos] = '\0';

  proto = MLuaParse(L, BigFnSource, pos, "bigfn");
  ASSERT_EQ(proto, NULL);
  ASSERT_NE(L->ErrorMsg, NULL);
  ASSERT_EQ(StrCmp(L->ErrorMsg, "function too large"), 0);
}

TEST(Empty) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "";

  MLuaProto *proto = MLuaParse(L, src, 0, "test");
  ASSERT_NE(proto, NULL);
  ASSERT(proto->CodeSize > 0); /* At least implicit RETURN */
}

TEST(Return) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return 42";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
  /* Should have LOADK and RETURN */
  ASSERT(proto->CodeSize >= 2);
}

TEST(LocalVar) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local x = 10";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(LocalMultiple) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local a, b, c = 1, 2, 3";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(Assignment) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "x = 5";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(Arithmetic) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local x = 1 + 2 * 3";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(IfThenElse) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "if true then local x = 1 else local x = 2 end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(WhileLoop) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "while true do local x = 1 end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(RepeatUntil) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "repeat local x = 1 until true";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(TableConstructor) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local t = {1, 2, x = 3}";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(FunctionCall) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "print(1, 2, 3)";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(NumericFor) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "for i = 1, 10 do local x = i end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(NumericForWithStep) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "for i = 10, 1, -1 do local x = i end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(GenericFor) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "for k, v in pairs(t) do local x = k end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(FunctionDecl) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "function foo(a, b) return a + b end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(Break) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "while true do break end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(NestedControl) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src =
      "if true then while true do if false then break end end end";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

TEST(AndOr) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local x = a and b or c";

  MLuaProto *proto = MLuaParse(L, src, StrLen(src), "test");
  ASSERT_NE(proto, NULL);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Parser Tests\n");
  printf("=====================\n\n");

  printf("Basic Parsing:\n");
  RUN_TEST(FunctionTooLarge);
  RUN_TEST(Empty);
  RUN_TEST(Return);
  RUN_TEST(LocalVar);
  RUN_TEST(LocalMultiple);
  RUN_TEST(Assignment);
  RUN_TEST(Arithmetic);

  printf("\nControl Flow:\n");
  RUN_TEST(IfThenElse);
  RUN_TEST(WhileLoop);
  RUN_TEST(RepeatUntil);
  RUN_TEST(NumericFor);
  RUN_TEST(NumericForWithStep);
  RUN_TEST(GenericFor);
  RUN_TEST(Break);
  RUN_TEST(NestedControl);

  printf("\nExpressions:\n");
  RUN_TEST(TableConstructor);
  RUN_TEST(FunctionCall);
  RUN_TEST(AndOr);

  printf("\nDeclarations:\n");
  RUN_TEST(FunctionDecl);

  printf("\n=====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
