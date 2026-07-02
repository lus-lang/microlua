/*
 * MicroLua - TestTable.c
 * Tests for MLuaTable.c (tables)
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaString.h"
#include "MLuaTable.h"
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

#define TEST_HEAP_SIZE (128 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

/* ========================================================================== */
/* Creation Tests                                                             */
/* ========================================================================== */

TEST(Create_Empty) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(IsTable(tbl));
  ASSERT_EQ(MLuaTableLen(tbl), 0);
}

TEST(Create_Sized) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNewSized(L, 10, 5);
  ASSERT(IsTable(tbl));
  ASSERT_EQ(MLuaTableLen(tbl), 0);
}

TEST(Create_SmallHintsUseInlineStorage) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  ASSERT_NE(L, NULL);

  MLuaValue arrayTbl = MLuaTableNewSized(L, 3, 0);
  gch = (MLuaGCHeader *)GetPtr(arrayTbl);
  th = MLUA_TABLEHEADER(gch);
  ASSERT(MLuaTableArrayIsInline(th));
  ASSERT(!MLuaTableHashIsInline(th));

  MLuaValue hashTbl = MLuaTableNewSized(L, 0, 1);
  gch = (MLuaGCHeader *)GetPtr(hashTbl);
  th = MLUA_TABLEHEADER(gch);
  ASSERT(!MLuaTableArrayIsInline(th));
  ASSERT(MLuaTableHashIsInline(th));

  MLuaValue mixedTbl = MLuaTableNewSized(L, 3, 1);
  gch = (MLuaGCHeader *)GetPtr(mixedTbl);
  th = MLUA_TABLEHEADER(gch);
  ASSERT(MLuaTableArrayIsInline(th));
  ASSERT(MLuaTableHashIsInline(th));
}

/* ========================================================================== */
/* Array Part Tests                                                           */
/* ========================================================================== */

TEST(Array_SetGet) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(MLuaTableSet(L, tbl, MakeInt(1), MakeInt(100)));

  MLuaValue v = MLuaTableGet(L, tbl, MakeInt(1));
  ASSERT_EQ(GetInt(v), 100);
  ASSERT_EQ(MLuaTableLen(tbl), 1);
}

TEST(Array_Sequential) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(MLuaTableSet(L, tbl, MakeInt(1), MakeInt(10)));
  ASSERT(MLuaTableSet(L, tbl, MakeInt(2), MakeInt(20)));
  ASSERT(MLuaTableSet(L, tbl, MakeInt(3), MakeInt(30)));

  ASSERT_EQ(MLuaTableLen(tbl), 3);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, MakeInt(2))), 20);
}

TEST(Array_PromotesAfterInlineCapacity) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(MLuaTableSet(L, tbl, MakeInt(1), MakeInt(10)));
  ASSERT(MLuaTableSet(L, tbl, MakeInt(2), MakeInt(20)));
  ASSERT(MLuaTableSet(L, tbl, MakeInt(3), MakeInt(30)));
  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);
  ASSERT(MLuaTableArrayIsInline(th));

  ASSERT(MLuaTableSet(L, tbl, MakeInt(4), MakeInt(40)));
  ASSERT(!MLuaTableArrayIsInline(th));
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, MakeInt(1))), 10);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, MakeInt(4))), 40);
}

TEST(Array_Append) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(MLuaTableAppend(L, tbl, MakeInt(1)));
  ASSERT(MLuaTableAppend(L, tbl, MakeInt(2)));
  ASSERT(MLuaTableAppend(L, tbl, MakeInt(3)));

  ASSERT_EQ(MLuaTableLen(tbl), 3);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, MakeInt(3))), 3);
}

TEST(Array_NoHoles) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(MLuaTableSet(L, tbl, MakeInt(1), MakeInt(10)));

  /* Setting index 3 while index 2 is absent would create a hole: rejected,
     and the value must not leak into the hash part. */
  ASSERT(!MLuaTableSet(L, tbl, MakeInt(3), MakeInt(30)));
  ASSERT(IsNil(MLuaTableGet(L, tbl, MakeInt(3))));
  ASSERT_EQ(MLuaTableLen(tbl), 1);

  /* Assigning nil to a would-be hole is a harmless no-op, not an error. */
  ASSERT(MLuaTableSet(L, tbl, MakeInt(3), MLUA_NIL));
  ASSERT_EQ(MLuaTableLen(tbl), 1);

  /* Filling the gap contiguously still works afterwards. */
  ASSERT(MLuaTableSet(L, tbl, MakeInt(2), MakeInt(20)));
  ASSERT(MLuaTableSet(L, tbl, MakeInt(3), MakeInt(30)));
  ASSERT_EQ(MLuaTableLen(tbl), 3);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, MakeInt(3))), 30);
}

/* ========================================================================== */
/* Hash Part Tests                                                            */
/* ========================================================================== */

TEST(Hash_StringKey) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  MLuaValue key = MLuaStringNew(L, "name", 4);

  ASSERT(MLuaTableSet(L, tbl, key, MakeInt(42)));
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, key)), 42);
}

TEST(Hash_Multiple) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  MLuaValue k1 = MLuaStringNew(L, "one", 3);
  MLuaValue k2 = MLuaStringNew(L, "two", 3);
  MLuaValue k3 = MLuaStringNew(L, "three", 5);

  ASSERT(MLuaTableSet(L, tbl, k1, MakeInt(1)));
  ASSERT(MLuaTableSet(L, tbl, k2, MakeInt(2)));
  ASSERT(MLuaTableSet(L, tbl, k3, MakeInt(3)));

  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, k1)), 1);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, k2)), 2);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, k3)), 3);
}

TEST(Hash_PromotesAfterInlineCapacity) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaGCHeader *gch;
  MLuaTableHeader *th;
  MLuaValue tbl;
  MLuaValue k1;
  MLuaValue k2;
  ASSERT_NE(L, NULL);

  tbl = MLuaTableNew(L);
  k1 = MLuaStringNew(L, "one", 3);
  k2 = MLuaStringNew(L, "two", 3);
  ASSERT(MLuaTableSet(L, tbl, k1, MakeInt(1)));
  gch = (MLuaGCHeader *)GetPtr(tbl);
  th = MLUA_TABLEHEADER(gch);
  ASSERT(MLuaTableHashIsInline(th));

  ASSERT(MLuaTableSet(L, tbl, k2, MakeInt(2)));
  ASSERT(!MLuaTableHashIsInline(th));
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, k1)), 1);
  ASSERT_EQ(GetInt(MLuaTableGet(L, tbl, k2)), 2);
}

TEST(Hash_Delete) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  MLuaValue key = MLuaStringNew(L, "key", 3);

  ASSERT(MLuaTableSet(L, tbl, key, MakeInt(100)));
  ASSERT(!IsNil(MLuaTableGet(L, tbl, key)));

  /* Delete by setting to nil */
  ASSERT(MLuaTableSet(L, tbl, key, MLUA_NIL));
  ASSERT(IsNil(MLuaTableGet(L, tbl, key)));
}

/* ========================================================================== */
/* Forward Delegation Tests                                                   */
/* ========================================================================== */

TEST(Forward_Basic) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue proto = MLuaTableNew(L);
  MLuaValue child = MLuaTableNew(L);

  MLuaValue key = MLuaStringNew(L, "inherited", 9);
  ASSERT(MLuaTableSet(L, proto, key, MakeInt(999)));

  MLuaTableSetForward(child, proto);

  /* Child should find value in prototype */
  ASSERT_EQ(GetInt(MLuaTableGet(L, child, key)), 999);
}

TEST(Forward_Override) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue proto = MLuaTableNew(L);
  MLuaValue child = MLuaTableNew(L);

  MLuaValue key = MLuaStringNew(L, "value", 5);
  ASSERT(MLuaTableSet(L, proto, key, MakeInt(1)));
  ASSERT(MLuaTableSet(L, child, key, MakeInt(2)));

  MLuaTableSetForward(child, proto);

  /* Child's value should override prototype */
  ASSERT_EQ(GetInt(MLuaTableGet(L, child, key)), 2);
}

#if MLUA_TABLE_NUM_ARRAYS
/* ========================================================================== */
/* Typed Numeric Array Tests (knob-on builds only)                            */
/* ========================================================================== */

#include "MLuaGC.h"

TEST(NumArray_SurvivesCompaction) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaGCRef tblRef;
  MLuaTableHeader *th;
  MLuaValue v;
  int round;
  Size i;
  ASSERT_NE(L, NULL);

  MLuaGCEnable(L, FALSE);
  MLuaPushGCRef(L, &tblRef, MLuaTableNew(L));

  /* Seed with a heap float: the array part must adopt the NUM kind */
  ASSERT(MLuaTableSet(L, tblRef.Value, MakeInt(1), MLuaMakeFloat(L, 1.5)));
  th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tblRef.Value));
  ASSERT_EQ(MLuaTableArrayKind(th), MLUA_TABLE_ARRAY_NUM);
  ASSERT_EQ(th->ArrayLen, 0); /* the no-regression invariant */

  for (i = 2; i <= 40; i++) {
    ASSERT(MLuaTableSet(L, tblRef.Value, MakeInt((I32)i),
                        MLuaMakeFloat(L, (double)i + 0.5)));
  }
  ASSERT_EQ(MLuaTableLen(tblRef.Value), 40);

  /* Repeated collections must remap the raw buffer without scanning its
   * float bits as values; contents stay exact across moves. */
  for (round = 0; round < 3; round++) {
    MLuaAllocObject(L, OBJTYPE_STRING, 100 + (Size)round); /* garbage */
    MLuaGCCollect(L);

    th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tblRef.Value));
    ASSERT_EQ(MLuaTableArrayKind(th), MLUA_TABLE_ARRAY_NUM);
    for (i = 1; i <= 40; i++) {
      v = MLuaTableGet(L, tblRef.Value, MakeInt((I32)i));
      ASSERT(MLuaToNumber(v) == (double)i + 0.5);
    }
  }

  /* Demotion in place: prior values preserved, kind locked */
  ASSERT(MLuaTableSet(L, tblRef.Value, MakeInt(2),
                      MLuaStringNew(L, "demoted", 7)));
  th = MLUA_TABLEHEADER((MLuaGCHeader *)GetPtr(tblRef.Value));
  ASSERT_EQ(MLuaTableArrayKind(th), MLUA_TABLE_ARRAY_LOCKED);
  ASSERT_EQ(MLuaTableLen(tblRef.Value), 40);
  v = MLuaTableGet(L, tblRef.Value, MakeInt(1));
  ASSERT(MLuaToNumber(v) == 1.5);
  v = MLuaTableGet(L, tblRef.Value, MakeInt(40));
  ASSERT(MLuaToNumber(v) == 40.5);

  MLuaPopGCRef(L, &tblRef);
}
#endif /* MLUA_TABLE_NUM_ARRAYS */

/* ========================================================================== */
/* Iteration Tests                                                            */
/* ========================================================================== */

TEST(Iteration_Array) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  MLuaValue tbl = MLuaTableNew(L);
  ASSERT(MLuaTableAppend(L, tbl, MakeInt(10)));
  ASSERT(MLuaTableAppend(L, tbl, MakeInt(20)));

  MLuaValue value;
  MLuaValue key = MLuaTableNext(L, tbl, MLUA_NIL, &value);

  ASSERT(!IsNil(key));
  ASSERT_EQ(GetInt(key), 1);
  ASSERT_EQ(GetInt(value), 10);

  key = MLuaTableNext(L, tbl, key, &value);
  ASSERT(!IsNil(key));
  ASSERT_EQ(GetInt(key), 2);
  ASSERT_EQ(GetInt(value), 20);

  key = MLuaTableNext(L, tbl, key, &value);
  ASSERT(IsNil(key)); /* End of iteration */
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Table Tests\n");
  printf("====================\n\n");

  printf("Creation:\n");
  RUN_TEST(Create_Empty);
  RUN_TEST(Create_Sized);
  RUN_TEST(Create_SmallHintsUseInlineStorage);

  printf("\nArray Part:\n");
  RUN_TEST(Array_SetGet);
  RUN_TEST(Array_Sequential);
  RUN_TEST(Array_PromotesAfterInlineCapacity);
  RUN_TEST(Array_Append);
  RUN_TEST(Array_NoHoles);

  printf("\nHash Part:\n");
  RUN_TEST(Hash_StringKey);
  RUN_TEST(Hash_Multiple);
  RUN_TEST(Hash_PromotesAfterInlineCapacity);
  RUN_TEST(Hash_Delete);

  printf("\nForward Delegation:\n");
  RUN_TEST(Forward_Basic);
  RUN_TEST(Forward_Override);

  printf("\nIteration:\n");
  RUN_TEST(Iteration_Array);

#if MLUA_TABLE_NUM_ARRAYS
  printf("\nTyped Numeric Arrays:\n");
  RUN_TEST(NumArray_SurvivesCompaction);
#endif

  printf("\n====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
