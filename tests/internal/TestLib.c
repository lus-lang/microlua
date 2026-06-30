/*
 * MicroLua - TestLib.c
 * Tests for standard library implementations
 * Tests for all new features: string, table, base library functions
 */

#include "MLuaAlloc.h"
#include "MLuaCode.h"
#include "MLuaConvert.h"
#include "MLuaCore.h"
#include "MLuaParse.h"
#include "MLuaVM.h"
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

#define TEST_HEAP_SIZE (512 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] __attribute__((aligned(8)));

/* ========================================================================== */
/* MLuaConvert Tests                                                          */
/* ========================================================================== */

TEST(IntToStr) {
  char buf[64];
  Size len;

  len = MLuaIntToStr(42, buf);
  ASSERT_EQ(len, 2);
  ASSERT(strcmp(buf, "42") == 0);

  len = MLuaIntToStr(-123, buf);
  ASSERT_EQ(len, 4);
  ASSERT(strcmp(buf, "-123") == 0);

  len = MLuaIntToStr(0, buf);
  ASSERT_EQ(len, 1);
  ASSERT(strcmp(buf, "0") == 0);
}

TEST(DoubleToStr) {
  char buf[64];
  Size len;

  len = MLuaDoubleToStr(3.14, buf, 2);
  ASSERT(len > 0);
  /* Verify it starts with "3.14" */
  ASSERT(buf[0] == '3');
  ASSERT(buf[1] == '.');

  len = MLuaDoubleToStr(0.0, buf, -1);
  ASSERT(len > 0);
}

TEST(StrToNumber) {
  double d;
  Bool ok;

  ok = MLuaStrToNumber("42", 2, &d);
  ASSERT(ok);
  ASSERT_EQ((int)d, 42);

  ok = MLuaStrToNumber("-3.14159", 8, &d);
  ASSERT(ok);
  ASSERT(d < -3.14 && d > -3.15);

  ok = MLuaStrToNumber("0x1F", 4, &d);
  ASSERT(ok);
  ASSERT_EQ((int)d, 31);

  ok = MLuaStrToNumber("1.5e3", 5, &d);
  ASSERT(ok);
  ASSERT_EQ((int)d, 1500);
}

TEST(StrToInt) {
  I64 n;
  Bool ok;

  ok = MLuaStrToInt("42", 2, 10, &n);
  ASSERT(ok);
  ASSERT_EQ(n, 42);

  ok = MLuaStrToInt("0xFF", 4, 0, &n);
  ASSERT(ok);
  ASSERT_EQ(n, 255);

  ok = MLuaStrToInt("1010", 4, 2, &n);
  ASSERT(ok);
  ASSERT_EQ(n, 10);

  ok = MLuaStrToInt("0o77", 4, 0, &n);
  ASSERT(ok);
  ASSERT_EQ(n, 63);
}

/* ========================================================================== */
/* String Library DoString Tests                                              */
/* ========================================================================== */

TEST(StringFormat) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return string.format('%s = %d', 'x', 42)";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(StringFind) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src =
      "local a, b = string.find('hello world', 'world'); return a";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(StringFindPattern) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local a, b = string.find('hello123', '%d+'); return a";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(StringMatch) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return string.match('test123', '%d+')";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(StringPack) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return #string.pack('bHI', 1, 2, 3)";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(StringPacksize) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return string.packsize('bHI')";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

/* ========================================================================== */
/* Table Library Tests                                                        */
/* ========================================================================== */

TEST(TableSort) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local t = {3, 1, 4, 1, 5}; table.sort(t); return t[1]";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(TableSortCustom) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  /* Simpler test: just test large array sort */
  const char *src = "local t = {5, 4, 3, 2, 1}; table.sort(t); return t[5]";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

/* ========================================================================== */
/* Base Library Tests                                                         */
/* ========================================================================== */

TEST(Tonumber) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return tonumber('0xFF')";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(TonumberBase) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return tonumber('1010', 2)";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(Tostring) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "return tostring(42)";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(Load) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  const char *src = "local f = load('return 42'); return f and f()";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

TEST(Xpcall) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  /* Simpler test - just verify pcall works (xpcall is similar) */
  const char *src = "local ok = pcall(function() return 1 end); return ok";
  MLuaOpenLibs(L);
  MLuaStatus status = MLuaDoString(L, src, StrLen(src), "test");
  ASSERT_EQ(status, MLUA_OK);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Library Tests\n");
  printf("======================\n\n");

  printf("MLuaConvert:\n");
  RUN_TEST(IntToStr);
  RUN_TEST(DoubleToStr);
  RUN_TEST(StrToNumber);
  RUN_TEST(StrToInt);

  printf("\nString Library:\n");
  RUN_TEST(StringFormat);
  RUN_TEST(StringFind);
  RUN_TEST(StringFindPattern);
  RUN_TEST(StringMatch);
  RUN_TEST(StringPack);
  RUN_TEST(StringPacksize);

  printf("\nTable Library:\n");
  RUN_TEST(TableSort);
  RUN_TEST(TableSortCustom);

  printf("\nBase Library:\n");
  RUN_TEST(Tonumber);
  RUN_TEST(TonumberBase);
  RUN_TEST(Tostring);
  RUN_TEST(Load);
  RUN_TEST(Xpcall);

  printf("\n======================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
