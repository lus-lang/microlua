/*
 * MicroLua - TestGC.c
 * Tests for MLuaGC.c (garbage collector)
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaGC.h"
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

/*
 * Pins MLuaNextGCThreshold across its three regimes: tight heap (the
 * classic live-growth allowance; the headroom term must degenerate --
 * this is what keeps bench min-heap numbers stable), roomy heap (the
 * headroom term dominates), and near-full heap (geometric half-remaining
 * batch). Synthetic `used` figures against the state's real HeapSize.
 */
TEST(GCThreshold_Pacing) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  ASSERT_NE(L, NULL);

  Size heap = L->HeapSize;
  Size reserve = heap / 8;
  if (reserve < MLUA_GC_RESERVE_MIN) {
    reserve = MLUA_GC_RESERVE_MIN;
  }
  if (reserve > MLUA_GC_RESERVE_MAX) {
    reserve = MLUA_GC_RESERVE_MAX;
  }

  {
    /* Tight regime: at 75% used the live-growth term dominates any
     * headroom term (free/DIV < live*0.75), so the result must equal the
     * classic pre-headroom formula exactly. */
    Size used = (heap * 3) / 4;
    Size classic = used + (used * MLUA_DEFAULT_GC_THRESHOLD_PERCENT) / 100;
    if (classic > heap - reserve) {
      classic = heap - reserve;
    }
    ASSERT_EQ(MLuaNextGCThreshold(L, used), classic);
  }

#if MLUA_GC_HEADROOM_DIV
  {
    /* Roomy regime: tiny live set -> allowance is free/DIV, above both
     * the live-growth percentage and the growth floor */
    Size used = heap / 64;
    Size expect = used + (heap - used) / MLUA_GC_HEADROOM_DIV;
    if (expect > heap - reserve) {
      expect = heap - reserve;
    }
    ASSERT_EQ(MLuaNextGCThreshold(L, used), expect);
  }
#endif

  {
    /* Near-full regime: live above the ceiling -> geometric batch of
     * half the remaining space (floored at 256) */
    Size used = heap - reserve / 2;
    Size batch = (heap - used) / 2;
    if (batch < 256) {
      batch = 256;
    }
    ASSERT_EQ(MLuaNextGCThreshold(L, used), used + batch);
  }
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
/* Header Layout Tests                                                        */
/* ========================================================================== */

/* Walk the heap by CachedSize verifying the invariants the header-packing
 * knob (MLUA_GC_HEADER_ALIGN) relies on: every object address and span is
 * MLUA_ALIGNMENT-aligned and carries a plausible type. */
static int WalkHeap(MLuaState *L) {
  U8 *scan = L->HeapBase + MLuaFirstObjOffset(L);
  U8 *heapEnd = L->HeapBase + L->HeapTop;
  int objects = 0;
  while (scan < heapEnd) {
    MLuaGCHeader *obj = (MLuaGCHeader *)scan;
    Size objSize = obj->CachedSize;
    U8 type = MLUA_OBJTYPE(obj);
    if ((UPtr)scan % MLUA_ALIGNMENT != 0 || objSize == 0 ||
        objSize % MLUA_ALIGNMENT != 0 || objSize > L->HeapSize ||
        type > 0x0A) {
      return -1;
    }
    scan += objSize;
    objects++;
  }
  return (scan == heapEnd) ? objects : -1;
}

TEST(GCHeaderPackedWalk) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  static const char longStr[] =
      "a string long enough to always live on the heap";
  MLuaGCRef strRef, tblRef, numRef, intRef;
  MLuaValue elem;
  int round;
  ASSERT_NE(L, NULL);

  MLuaGCEnable(L, FALSE);

  /* A mix of object shapes: interned string, table with array + hash parts,
   * a heap number, a (possibly boxed) large integer, and garbage between. */
  MLuaPushGCRef(L, &strRef, MLuaStringNew(L, longStr, sizeof(longStr) - 1));
  MLuaPushGCRef(L, &tblRef, MLuaTableNew(L));
  MLuaPushGCRef(L, &numRef, MLuaMakeFloat(L, 3.25));
  MLuaPushGCRef(L, &intRef, MLuaMakeIntSafe(L, (I32)0x7FFFFFF0));
  ASSERT(IsPtr(strRef.Value));
  ASSERT(MLuaTableSet(L, tblRef.Value, MakeInt(1), MLuaMakeFloat(L, 1.5)));
  ASSERT(MLuaTableSet(L, tblRef.Value, strRef.Value, MakeInt(7)));

  ASSERT_GT(WalkHeap(L), 0);

  /* Repeated collections move everything; contents must survive intact and
   * the heap must stay linearly walkable each time. */
  for (round = 0; round < 3; round++) {
    MLuaAllocObject(L, OBJTYPE_STRING, 40 + (Size)round); /* garbage */
    MLuaGCCollect(L);
    ASSERT_GT(WalkHeap(L), 0);

    ASSERT(MLuaStringLen(strRef.Value) == sizeof(longStr) - 1);
    ASSERT(MLuaToNumber(numRef.Value) == 3.25);
    ASSERT(MLuaGetIntVal(intRef.Value) == (I32)0x7FFFFFF0);
    elem = MLuaTableGet(L, tblRef.Value, MakeInt(1));
    ASSERT(MLuaToNumber(elem) == 1.5);
    elem = MLuaTableGet(L, tblRef.Value, strRef.Value);
    ASSERT(IsInt(elem) && MLuaGetIntVal(elem) == 7);
  }

  MLuaPopGCRef(L, &intRef);
  MLuaPopGCRef(L, &numRef);
  MLuaPopGCRef(L, &tblRef);
  MLuaPopGCRef(L, &strRef);
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

/* Pointer keys hash by address; compaction moves them. The GC must leave
 * the table findable afterwards (GCFLAG_HASHSTALE + rehash-on-access), or
 * table-keyed entries silently vanish after any collection. */
TEST(GCCollect_PointerKeysSurviveCompaction) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaGCRef tref;
  MLuaGCRef kref[8];
  int i;
  ASSERT_NE(L, NULL);

  MLuaGCEnable(L, FALSE);

  MLuaPushGCRef(L, &tref, MLuaTableNew(L));

  /* Interleave garbage before each key so compaction slides the keys down */
  for (i = 0; i < 8; i++) {
    MLuaAllocObject(L, OBJTYPE_STRING, 128);
    MLuaPushGCRef(L, &kref[i], MLuaTableNew(L));
    MLuaTableSet(L, tref.Value, kref[i].Value, MakeInt(i + 1));
  }

  MLuaGCCollect(L);

  for (i = 0; i < 8; i++) {
    MLuaValue v = MLuaTableGet(L, tref.Value, kref[i].Value);
    ASSERT(IsInt(v));
    ASSERT_EQ(GetInt(v), i + 1);
  }

  /* A second cycle must not regress what the rehash repaired */
  MLuaGCCollect(L);
  for (i = 0; i < 8; i++) {
    MLuaValue v = MLuaTableGet(L, tref.Value, kref[i].Value);
    ASSERT(IsInt(v));
    ASSERT_EQ(GetInt(v), i + 1);
  }

  for (i = 7; i >= 0; i--) {
    MLuaPopGCRef(L, &kref[i]);
  }
  MLuaPopGCRef(L, &tref);
}

/* A collection that shrinks the (weak) intern table must not recurse into a
 * second full collection: the abandoned backing array is ordinary garbage
 * for the NEXT cycle. One MLuaGCCollect call = exactly one cycle. */
TEST(GCCollect_InternShrinkIsSingleCycle) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  U32 cyclesBefore;
  char buf[32];
  int i;
  ASSERT_NE(L, NULL);

  MLuaGCEnable(L, FALSE);

  /* Intern enough distinct heap strings to grow the table well past its
   * initial capacity, holding no references to any of them. */
  for (i = 0; i < 600; i++) {
    int n = i;
    int p = 0;
    buf[p++] = 'i';
    buf[p++] = 'n';
    buf[p++] = 't';
    buf[p++] = 'e';
    buf[p++] = 'r';
    buf[p++] = 'n';
    do {
      buf[p++] = (char)('0' + (n % 10));
      n /= 10;
    } while (n > 0);
    MLuaStringNew(L, buf, (Size)p);
  }

  /* The dead strings must have grown the intern table, or this test would
   * pass without exercising the shrink path at all. */
  {
    Size capBefore = L->StringTableCap;
    cyclesBefore = L->GCCycleCount;
    MLuaGCCollect(L); /* drops the dead strings; the intern table shrinks */
    ASSERT_GT(capBefore, L->StringTableCap);
    ASSERT_EQ(L->GCCycleCount, cyclesBefore + 1);
  }

  /* Interning must still work against the migrated table. */
  {
    MLuaValue a = MLuaStringNew(L, "post-shrink-string", 18);
    MLuaValue b = MLuaStringNew(L, "post-shrink-string", 18);
    ASSERT_EQ(a, b); /* pointer equality == value equality */
  }
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua GC Tests\n");
  printf("=================\n\n");

  printf("GC Control:\n");
  RUN_TEST(GCEnable);
  RUN_TEST(GCHeaderPackedWalk);
  RUN_TEST(GCThreshold);
  RUN_TEST(GCThreshold_Pacing);

  printf("\nGCRef:\n");
  RUN_TEST(GCRef_PushPop);
  RUN_TEST(GCRef_Multiple);

  printf("\nCollection:\n");
  RUN_TEST(GCCollect_Empty);
  RUN_TEST(GCCollect_UnreferencedObjects);
  RUN_TEST(GCCollect_ReferencedOnStack);
  RUN_TEST(GCCollect_ReferencedByGCRef);
  RUN_TEST(GCCollect_PointerKeysSurviveCompaction);
  RUN_TEST(GCCollect_InternShrinkIsSingleCycle);

  printf("\nMarking:\n");
  RUN_TEST(GCMark_NonPointer);
  RUN_TEST(GCMark_Object);

  printf("\n=================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
