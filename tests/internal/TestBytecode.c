/*
 * MicroLua - TestBytecode.c
 * Portable bytecode dump/load tests.
 */

#include "MLuaAlloc.h"
#include "MLuaDump.h"
#include "MLuaString.h"
#include "MLuaVM.h"
#include <stdio.h>

#define TEST_HEAP_SIZE (256 * 1024)
static U8 TestHeapA[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);
static U8 TestHeapB[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

static int TestsPassed = 0;
static int TestsFailed = 0;

#define TEST(name) static void Test_##name(void)
#define RUN_TEST(name)                                                         \
  do {                                                                         \
    printf("  Testing %s... ", #name);                                         \
    Test_##name();                                                             \
    printf("OK\n");                                                           \
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

static Size CompileDump(const char *src, char *buf, Size cap, int endian) {
  MLuaState *L = MLuaStateInit(TestHeapA, TEST_HEAP_SIZE);
  MLuaStatus st;
  MLuaValue func;

  MLuaOpenLibs(L);
  st = MLuaLoadString(L, src, StrLen(src), "bytecode-test");
  if (st != MLUA_OK) {
    return 0;
  }
  func = MLuaPop(L);
  return MLuaDumpFunctionEndian(L, func, buf, cap, endian);
}

static void RunDumpedReturn(int endian) {
  const char *src = "local x = 21 * 2; return x";
  char buf[4096];
  Size size = CompileDump(src, NULL, 0, endian);
  MLuaState *L;
  MLuaStatus st;
  MLuaValue result;

  ASSERT(size > 0);
  ASSERT(size <= sizeof(buf));
  ASSERT_EQ(CompileDump(src, buf, sizeof(buf), endian), size);

  L = MLuaStateInit(TestHeapB, TEST_HEAP_SIZE);
  MLuaOpenLibs(L);
  st = MLuaDoBytecode(L, buf, size, "bytecode-test");
  ASSERT_EQ(st, MLUA_OK);
  result = MLuaPop(L);
  ASSERT(IsInt(result));
  ASSERT_EQ(GetInt(result), 42);
}

TEST(RoundTripLittleEndian) {
  RunDumpedReturn(MLUA_BYTECODE_ENDIAN_LITTLE);
}

TEST(RoundTripBigEndian) { RunDumpedReturn(MLUA_BYTECODE_ENDIAN_BIG); }

TEST(RejectBadMagic) {
  char bad[13] = {0};
  MLuaState *L = MLuaStateInit(TestHeapB, TEST_HEAP_SIZE);
  ASSERT_EQ(MLuaLoadBytecode(L, bad, sizeof(bad), "bad"), MLUA_ERRRUN);
}

TEST(RejectBadVersion) {
  const char *src = "return 1";
  char buf[512];
  Size size = CompileDump(src, buf, sizeof(buf), MLUA_BYTECODE_ENDIAN_LITTLE);
  MLuaState *L;

  ASSERT(size > 5);
  buf[4] = 0x7F;

  L = MLuaStateInit(TestHeapB, TEST_HEAP_SIZE);
  ASSERT_EQ(MLuaLoadBytecode(L, buf, size, "bad"), MLUA_ERRRUN);
}

TEST(RejectBadEndian) {
  const char *src = "return 1";
  char buf[512];
  Size size = CompileDump(src, buf, sizeof(buf), MLUA_BYTECODE_ENDIAN_LITTLE);
  MLuaState *L;

  ASSERT(size > 7);
  buf[6] = 0x7F;

  L = MLuaStateInit(TestHeapB, TEST_HEAP_SIZE);
  ASSERT_EQ(MLuaLoadBytecode(L, buf, size, "bad"), MLUA_ERRRUN);
}

TEST(RejectTruncatedChunk) {
  const char *src = "return 1";
  char buf[512];
  Size size = CompileDump(src, buf, sizeof(buf), MLUA_BYTECODE_ENDIAN_LITTLE);
  MLuaState *L = MLuaStateInit(TestHeapB, TEST_HEAP_SIZE);

  ASSERT(size > 20);
  ASSERT_EQ(MLuaLoadBytecode(L, buf, size - 1, "bad"), MLUA_ERRRUN);
}

int main(void) {
  printf("MicroLua Bytecode Tests\n");
  printf("=======================\n\n");

  printf("Portable bytecode:\n");
  RUN_TEST(RoundTripLittleEndian);
  RUN_TEST(RoundTripBigEndian);
  RUN_TEST(RejectBadMagic);
  RUN_TEST(RejectBadVersion);
  RUN_TEST(RejectBadEndian);
  RUN_TEST(RejectTruncatedChunk);

  printf("\nResults: %d passed, %d failed\n", TestsPassed, TestsFailed);
  return TestsFailed > 0 ? 1 : 0;
}
