/*
 * MicroLua - TestAlloc.c
 * Tests for MLuaAlloc.c (heap allocator)
 */

#include "MLuaAlloc.h"
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

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))

/* Test heap size */
#define TEST_HEAP_SIZE (64 * 1024) /* 64 KB */
static U8 TestHeap[TEST_HEAP_SIZE] __attribute__((aligned(8)));

/* ========================================================================== */
/* State Initialization Tests                                                 */
/* ========================================================================== */

TEST(StateInit_Basic) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);
  ASSERT_GT(MLuaMemoryTotal(L), 0);
  ASSERT_GT(MLuaMemoryFree(L), 0);
  ASSERT_GT(MLuaMemoryUsed(L), 0); /* State + stack is already allocated */
}

TEST(StateInit_TooSmall) {
  U8 tinyHeap[100];
  MLuaState *L = MLuaStateInit(tinyHeap, sizeof(tinyHeap));
  ASSERT_EQ(L, NULL); /* Should fail - too small */
}

TEST(StateInit_NullMemory) {
  MLuaState *L = MLuaStateInit(NULL, TEST_HEAP_SIZE);
  ASSERT_EQ(L, NULL); /* Should fail - null memory */
}

TEST(StateInit_EvalStackInitialized) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);
  ASSERT_NE(L->EvalStack, NULL);
  ASSERT_EQ(L->EvalTop, 0);
  ASSERT_EQ(L->EvalStackSize, MLUA_DEFAULT_STACK_SIZE);
  /* EvalStack should be initialized to nil */
  ASSERT(IsNil(L->EvalStack[0]));
}

/* ========================================================================== */
/* Allocation Tests                                                           */
/* ========================================================================== */

TEST(Alloc_Basic) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  Size usedBefore = MLuaMemoryUsed(L);

  void *ptr = MLuaAlloc(L, 100);
  ASSERT_NE(ptr, NULL);

  Size usedAfter = MLuaMemoryUsed(L);
  ASSERT_GT(usedAfter, usedBefore);
}

TEST(Alloc_Alignment) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  /* Allocate odd sizes, check alignment */
  void *p1 = MLuaAlloc(L, 1);
  void *p2 = MLuaAlloc(L, 7);
  void *p3 = MLuaAlloc(L, 13);

  ASSERT(IS_ALIGNED((UPtr)p1, MLUA_ALIGNMENT));
  ASSERT(IS_ALIGNED((UPtr)p2, MLUA_ALIGNMENT));
  ASSERT(IS_ALIGNED((UPtr)p3, MLUA_ALIGNMENT));
}

TEST(Alloc_ZeroSize) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  void *ptr = MLuaAlloc(L, 0);
  ASSERT_EQ(ptr, NULL); /* Zero-size allocation returns NULL */
}

TEST(Alloc_Sequential) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  void *p1 = MLuaAlloc(L, 64);
  void *p2 = MLuaAlloc(L, 64);
  void *p3 = MLuaAlloc(L, 64);

  ASSERT_NE(p1, NULL);
  ASSERT_NE(p2, NULL);
  ASSERT_NE(p3, NULL);

  /* Bump allocator: addresses should be sequential */
  ASSERT((U8 *)p2 > (U8 *)p1);
  ASSERT((U8 *)p3 > (U8 *)p2);
}

/* ========================================================================== */
/* Object Allocation Tests                                                    */
/* ========================================================================== */

TEST(AllocObject_Basic) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCHeader *obj = MLuaAllocObject(L, OBJTYPE_STRING, 32);
  ASSERT_NE(obj, NULL);
  ASSERT_EQ(MLUA_OBJTYPE(obj), OBJTYPE_STRING);
  ASSERT_EQ(obj->Flags & ~GCFLAG_TYPE_MASK, 0); /* No GC flags set */
}

TEST(AllocObject_DataAccess) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCHeader *obj = MLuaAllocObject(L, OBJTYPE_USERDATA, 100);
  ASSERT_NE(obj, NULL);

  /* Can write to data area */
  U8 *data = (U8 *)MLUA_OBJDATA(obj);
  data[0] = 0xAB;
  data[99] = 0xCD;
  ASSERT_EQ(data[0], 0xAB);
  ASSERT_EQ(data[99], 0xCD);

  /* Can get header back from data */
  MLuaGCHeader *header = MLUA_OBJHEADER(data);
  ASSERT_EQ(header, obj);
}

TEST(AllocObject_Size) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCHeader *obj = MLuaAllocObject(L, OBJTYPE_TABLE, 200);
  ASSERT_NE(obj, NULL);

  Size size = MLuaObjectSize(obj);
  ASSERT_GT(size, 200); /* Should be at least 200 + header */
}

/* ========================================================================== */
/* Memory Statistics Tests                                                    */
/* ========================================================================== */

TEST(MemoryStats) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  Size total = MLuaMemoryTotal(L);
  Size used = MLuaMemoryUsed(L);
  Size free = MLuaMemoryFree(L);

  ASSERT_EQ(used + free, total);

  /* Allocate some memory */
  MLuaAlloc(L, 1000);

  Size newUsed = MLuaMemoryUsed(L);
  Size newFree = MLuaMemoryFree(L);

  ASSERT_GT(newUsed, used);
  ASSERT_LT(newFree, free);
  ASSERT_EQ(newUsed + newFree, total);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Allocator Tests\n");
  printf("========================\n\n");

  printf("State Initialization:\n");
  RUN_TEST(StateInit_Basic);
  RUN_TEST(StateInit_TooSmall);
  RUN_TEST(StateInit_NullMemory);
  RUN_TEST(StateInit_EvalStackInitialized);

  printf("\nAllocation:\n");
  RUN_TEST(Alloc_Basic);
  RUN_TEST(Alloc_Alignment);
  RUN_TEST(Alloc_ZeroSize);
  RUN_TEST(Alloc_Sequential);

  printf("\nObject Allocation:\n");
  RUN_TEST(AllocObject_Basic);
  RUN_TEST(AllocObject_DataAccess);
  RUN_TEST(AllocObject_Size);

  printf("\nMemory Statistics:\n");
  RUN_TEST(MemoryStats);

  printf("\n========================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
