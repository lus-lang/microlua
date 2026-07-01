/*
 * MicroLua - TestGC.c
 * Tests for MLuaGC.c (garbage collector)
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaGC.h"
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
#define ASSERT_LE(a, b) ASSERT((a) <= (b))

/* Test heap size */
#define TEST_HEAP_SIZE (64 * 1024) /* 64 KB */
static U8 TestHeap[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

/* ========================================================================== */
/* GC Control Tests                                                           */
/* ========================================================================== */

TEST(GCEnable) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  ASSERT(MLuaGCIsEnabled(L)); /* Enabled by default */

  MLuaGCEnable(L, FALSE);
  ASSERT(!MLuaGCIsEnabled(L));

  MLuaGCEnable(L, TRUE);
  ASSERT(MLuaGCIsEnabled(L));
}

TEST(GCThreshold) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  Size threshold = L->GCThreshold;
  ASSERT_GT(threshold, 0);

  MLuaGCSetThreshold(L, 1000);
  ASSERT_EQ(L->GCThreshold, 1000);
}

/* ========================================================================== */
/* GCRef Tests                                                                */
/* ========================================================================== */

TEST(GCRef_PushPop) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCRef ref;
  MLuaValue val = MakeInt(42);

  MLuaPushGCRef(L, &ref, val);
  ASSERT_EQ(ref.Value, val);

  MLuaPopGCRef(L, &ref);
  ASSERT(IsNil(ref.Value));
}

TEST(GCRef_Multiple) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCRef ref1, ref2, ref3;

  MLuaPushGCRef(L, &ref1, MakeInt(1));
  MLuaPushGCRef(L, &ref2, MakeInt(2));
  MLuaPushGCRef(L, &ref3, MakeInt(3));

  ASSERT_EQ(GetInt(ref1.Value), 1);
  ASSERT_EQ(GetInt(ref2.Value), 2);
  ASSERT_EQ(GetInt(ref3.Value), 3);

  /* Pop in different order */
  MLuaPopGCRef(L, &ref2);
  ASSERT_EQ(GetInt(ref1.Value), 1);
  ASSERT_EQ(GetInt(ref3.Value), 3);

  MLuaPopGCRef(L, &ref1);
  MLuaPopGCRef(L, &ref3);
}

/* ========================================================================== */
/* Collection Tests                                                           */
/* ========================================================================== */

TEST(GCCollect_Empty) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  Size usedBefore = MLuaMemoryUsed(L);

  /* Collection on empty heap should not crash */
  MLuaGCCollect(L);

  Size usedAfter = MLuaMemoryUsed(L);
  ASSERT_LE(usedAfter, usedBefore); /* Should be same or less */
}

TEST(GCCollect_UnreferencedObjects) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  /* Disable auto-GC for controlled testing */
  MLuaGCEnable(L, FALSE);

  /* Allocate objects that are not referenced */
  MLuaAllocObject(L, OBJTYPE_STRING, 100);
  MLuaAllocObject(L, OBJTYPE_STRING, 200);
  MLuaAllocObject(L, OBJTYPE_STRING, 300);

  Size usedBefore = MLuaMemoryUsed(L);
  ASSERT_GT(usedBefore, 0);

  /* Run GC - unreferenced objects should be collected */
  MLuaGCCollect(L);

  Size usedAfter = MLuaMemoryUsed(L);
  /* Memory should be reclaimed */
  ASSERT_LE(usedAfter, usedBefore);
}

TEST(GCCollect_ReferencedOnStack) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCEnable(L, FALSE);

  /* Allocate an object and put it on the stack */
  MLuaGCHeader *obj = MLuaAllocObject(L, OBJTYPE_TABLE, 64);
  ASSERT_NE(obj, NULL);

  MLuaValue val = MakePtr(obj);
  L->EvalStack[0] = val;
  L->EvalTop = 1;

  /* Run GC - object should survive because it's on EvalStack */
  MLuaGCCollect(L);

  /* The value on EvalStack should still be valid (may have moved) */
  ASSERT(IsPtr(L->EvalStack[0]));
}

TEST(GCCollect_ReferencedByGCRef) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCEnable(L, FALSE);

  /* Allocate an object */
  MLuaGCHeader *obj = MLuaAllocObject(L, OBJTYPE_USERDATA, 32);
  ASSERT_NE(obj, NULL);

  MLuaValue val = MakePtr(obj);

  /* Reference it with GCRef */
  MLuaGCRef ref;
  MLuaPushGCRef(L, &ref, val);

  /* Run GC - object should survive */
  MLuaGCCollect(L);

  /* GCRef value should still be valid (may have moved) */
  ASSERT(IsPtr(ref.Value));

  MLuaPopGCRef(L, &ref);
}

/* ========================================================================== */
/* Mark Tests                                                                 */
/* ========================================================================== */

TEST(GCMark_NonPointer) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  /* Marking non-pointer values should not crash */
  MLuaGCMark(L, MLUA_NIL);
  MLuaGCMark(L, MLUA_TRUE);
  MLuaGCMark(L, MakeInt(42));
  MLuaGCMark(L, MakeShortStr('a', 'b', 'c'));
}

TEST(GCMark_Object) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaGCHeader *obj = MLuaAllocObject(L, OBJTYPE_STRING, 10);
  ASSERT_NE(obj, NULL);
  ASSERT(!MLuaGCIsMarked(obj));

  MLuaGCMark(L, MakePtr(obj));
  ASSERT(MLuaGCIsMarked(obj));
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua GC Tests\n");
  printf("=================\n\n");

  printf("GC Control:\n");
  RUN_TEST(GCEnable);
  RUN_TEST(GCThreshold);

  printf("\nGCRef:\n");
  RUN_TEST(GCRef_PushPop);
  RUN_TEST(GCRef_Multiple);

  printf("\nCollection:\n");
  RUN_TEST(GCCollect_Empty);
  RUN_TEST(GCCollect_UnreferencedObjects);
  RUN_TEST(GCCollect_ReferencedOnStack);
  RUN_TEST(GCCollect_ReferencedByGCRef);

  printf("\nMarking:\n");
  RUN_TEST(GCMark_NonPointer);
  RUN_TEST(GCMark_Object);

  printf("\n=================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
