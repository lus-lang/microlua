/*
 * MicroLua - TestCoro.c
 * Coroutine C-API tests and GC-under-memory-pressure stress tests.
 *
 * These tests run real Lua workloads in small constrained heaps so that the
 * mark-compact collector actually fires (the safepoint design collects at
 * instruction boundaries), exercising relocation of closures, upvalues,
 * protos, tables, strings and suspended coroutine contexts.
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaGC.h"
#include "MLuaString.h"
#include "MLuaThread.h"
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

#define TEST_HEAP_SIZE (96 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] MLUA_ALIGNAS(MLUA_ALIGNMENT);

static MLuaState *NewState(void) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  if (L) {
    MLuaOpenLibs(L);
  }
  return L;
}

/* Compare a Lua string value against a C string */
static Bool StrIs(MLuaValue v, const char *expect) {
  const char *s = MLuaStringData(v);
  Size len = MLuaStringLen(v);
  return s && len == StrLen(expect) && MemCmp(s, expect, len) == 0;
}

/* Run a script and require a global named 'result' to equal an int */
static Bool RunAndCheckInt(MLuaState *L, const char *src, I32 expected) {
  MLuaStatus st = MLuaDoString(L, src, StrLen(src), "test");
  MLuaValue v;
  if (st != MLUA_OK) {
    printf("(script error: %s) ", L->ErrorMsg ? L->ErrorMsg : "?");
    return FALSE;
  }
  v = MLuaGetGlobal(L, "result");
  if (!IsInt(v)) {
    printf("(result not an int) ");
    return FALSE;
  }
  if (GetInt(v) != expected) {
    printf("(result=%d expected=%d) ", (int)GetInt(v), (int)expected);
    return FALSE;
  }
  return TRUE;
}

/* ========================================================================== */
/* Coroutine C API                                                            */
/* ========================================================================== */

TEST(Coro_YieldOutsideCoroutine) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  /* Yield with no coroutine running must fail, not crash */
  ASSERT_EQ(MLuaThreadYield(L, 0), MLUA_ERRRUN);
  ASSERT_NE(L->ErrorMsg, NULL);
}

TEST(Coro_ResumeNonThread) {
  MLuaState *L = NewState();
  Size nres = 0;
  ASSERT_NE(L, NULL);

  ASSERT_EQ(MLuaThreadResume(L, MakeInt(42), NULL, 0, &nres), MLUA_ERRRUN);
  ASSERT_EQ(nres, 0);
}

TEST(Coro_StatusTransitions) {
  MLuaState *L = NewState();
  const char *src = "co = coroutine.create(function()\n"
                    "  coroutine.yield()\n"
                    "end)\n"
                    "s1 = coroutine.status(co)\n"
                    "coroutine.resume(co)\n"
                    "s2 = coroutine.status(co)\n"
                    "coroutine.resume(co)\n"
                    "s3 = coroutine.status(co)\n";
  ASSERT_NE(L, NULL);

  ASSERT_EQ(MLuaDoString(L, src, StrLen(src), "t"), MLUA_OK);
  ASSERT(StrIs(MLuaGetGlobal(L, "s1"), "suspended"));
  ASSERT(StrIs(MLuaGetGlobal(L, "s2"), "suspended"));
  ASSERT(StrIs(MLuaGetGlobal(L, "s3"), "dead"));
}

TEST(VM_FrameOverflow) {
  MLuaState *L = NewState();
  /* Non-tail recursion must hit the frame cap and ERROR, not crash.
   * (n + recurse(...) prevents the tail-call optimization.) */
  const char *src = "local function recurse(n) return 1 + recurse(n + 1) end\n"
                    "ok, err = pcall(recurse, 1)\n";
  ASSERT_NE(L, NULL);

  ASSERT_EQ(MLuaDoString(L, src, StrLen(src), "t"), MLUA_OK);
  ASSERT_EQ(MLuaGetGlobal(L, "ok"), MLUA_FALSE);
}

/* ========================================================================== */
/* GC Under Memory Pressure                                                   */
/* ========================================================================== */

TEST(GC_StringChurnReclaimed) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  /* Thousands of distinct transient strings would exhaust a 96KB heap many
   * times over; weak interning + compaction must reclaim them. */
  ASSERT(RunAndCheckInt(L,
                        "local n = 0\n"
                        "for i = 1, 3000 do\n"
                        "  local s = 'prefix-' .. i .. '-suffix'\n"
                        "  n = n + string.len(s)\n"
                        "end\n"
                        "result = n\n",
                        /* lengths: 'prefix--suffix' = 14 + digits */
                        14 * 3000 + 9 * 1 + 90 * 2 + 900 * 3 + 2001 * 4));
}

TEST(GC_TableChurnReclaimed) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  ASSERT(RunAndCheckInt(L,
                        "local total = 0\n"
                        "for i = 1, 2000 do\n"
                        "  local t = { i, i + 1, i + 2 }\n"
                        "  total = total + t[1] + t[3]\n"
                        "end\n"
                        "result = total\n",
                        /* sum of (i + i+2) for i=1..2000 */
                        2 * (2000 * 2001 / 2) + 2 * 2000));
}

TEST(GC_ClosureChurnSurvivesCompaction) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  /* Closures + upvalues created in bulk; a long-lived closure made early
   * must still work after many collections moved it around. */
  ASSERT(RunAndCheckInt(L,
                        "local function mk(v) return function() return v end "
                        "end\n"
                        "local keeper = mk(777)\n"
                        "local sum = 0\n"
                        "for i = 1, 2000 do\n"
                        "  local f = mk(i)\n"
                        "  sum = sum + f()\n"
                        "end\n"
                        "result = keeper() + (sum - sum)\n",
                        777));
}

TEST(GC_ClosedUpvalueSurvivesCompaction) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  /* The counter's upvalue is CLOSED when make() returns; its Location
   * self-pointer must be recomputed every time compaction moves it. */
  ASSERT(RunAndCheckInt(L,
                        "local function make()\n"
                        "  local count = 0\n"
                        "  return function()\n"
                        "    count = count + 1\n"
                        "    return count\n"
                        "  end\n"
                        "end\n"
                        "local counter = make()\n"
                        "for i = 1, 1500 do\n"
                        "  local junk = { 'churn' .. i }\n"
                        "  counter()\n"
                        "end\n"
                        "result = counter()\n",
                        1501));
}

TEST(GC_SuspendedCoroutineSurvivesCompaction) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  /* A coroutine suspended two Lua frames deep, holding locals and a
   * pending vararg count, must survive heavy allocation churn on the
   * main thread (its frames' protos/closures all relocate). */
  ASSERT(RunAndCheckInt(
      L,
      "local co = coroutine.create(function(base, ...)\n"
      "  local stash = 'kept-' .. base\n"
      "  local nv = select('#', ...)\n"
      "  local function inner(x)\n"
      "    coroutine.yield(x)\n"
      "    return nv + x\n"
      "  end\n"
      "  local r = inner(base * 2)\n"
      "  return r + string.len(stash)\n"
      "end)\n"
      "local ok1, y = coroutine.resume(co, 10, 'a', 'b', 'c')\n"
      "local churn = 0\n"
      "for i = 1, 1500 do\n"
      "  local junk = { data = 'x' .. i }\n"
      "  churn = churn + #junk.data\n"
      "end\n"
      "local ok2, fin = coroutine.resume(co)\n"
      /* fin = (3 varargs + 20) + len('kept-10')=7 -> 30 */
      "result = (ok1 and ok2) and (y * 100 + fin) or -1\n",
      20 * 100 + 30));
}

TEST(GC_ExplicitCollectWithGCRef) {
  MLuaState *L = NewState();
  MLuaGCRef ref;
  MLuaValue str;
  ASSERT_NE(L, NULL);

  /* A C-held reference must survive an explicit collection and be
   * remapped if the string moves. */
  str = MLuaStringNew(L, "anchored-string-payload", 23);
  ASSERT(IsPtr(str));
  MLuaPushGCRef(L, &ref, str);

  MLuaGCCollect(L);

  ASSERT(IsPtr(ref.Value));
  ASSERT(StrIs(ref.Value, "anchored-string-payload"));
  MLuaPopGCRef(L, &ref);
}

TEST(GC_HeapShrinksAfterChurn) {
  MLuaState *L = NewState();
  Size before;
  Size after;
  ASSERT_NE(L, NULL);

  const char *churn = "for i = 1, 500 do local t = { 'a' .. i } end\n";
  ASSERT_EQ(MLuaDoString(L, churn, StrLen(churn), "t"), MLUA_OK);

  before = MLuaMemoryUsed(L);
  MLuaGCCollect(L);
  after = MLuaMemoryUsed(L);

  /* Compaction must reclaim a meaningful share of the churned garbage */
  ASSERT(after < before);
}

/*
 * Non-zeroed allocations (MLuaAllocNC / MLuaAllocObjectNC) under real GC
 * pressure: accumulator concat and array growth allocate through the NC
 * paths, the 96 KB heap forces collections mid-loop, and the compactor
 * relocates freshly NC-allocated objects (including their uninitialized
 * padding tails). Exact final contents pin that no byte the runtime reads
 * was left to garbage.
 */
TEST(GC_NoClearAllocSurvivesCompaction) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  /* Accumulator concat: every step allocates the result via the NC path;
   * dead predecessors force repeated compaction in 96 KB. 400 iterations
   * of "xy" -> 800 chars + digits interleaved for content variety. */
  const char *concat = "local s = ''\n"
                       "for i = 1, 400 do s = s .. (i % 10) .. 'y' end\n"
                       "result = #s\n";
  ASSERT(RunAndCheckInt(L, concat, 800));

  /* Array + hash growth through the NC paths, with churn so collections
   * interleave; verify content integrity element by element. */
  const char *tables = "local t = {}\n"
                       "for i = 1, 2000 do t[i] = i * 3 end\n"
                       "local h = {}\n"
                       "for i = 1, 300 do h['k' .. i] = i end\n"
                       "local sum = 0\n"
                       "for i = 1, 2000 do sum = sum + (t[i] - i * 3) end\n"
                       "for i = 1, 300 do sum = sum + (h['k' .. i] - i) end\n"
                       "result = sum\n";
  ASSERT(RunAndCheckInt(L, tables, 0));

  /* string.rep / reverse / upper build through MLuaAllocNC scratch */
  const char *strfns = "local r = string.rep('ab', 200)\n"
                       "local v = string.reverse(r)\n"
                       "local u = string.upper(r)\n"
                       "result = #v + #u + (string.sub(v, 1, 2) == 'ba' and 1 or 0)\n"
                       "         + (string.sub(u, 1, 2) == 'AB' and 1 or 0)\n";
  ASSERT(RunAndCheckInt(L, strfns, 802));
}

/*
 * Pins the intern-table OOM fallback: when the heap is too full of
 * collectable garbage for the intern table to resize (allocations never
 * collect), new strings must still intern into the not-yet-grown table --
 * NEVER escape un-interned. The historical failure returned textually
 * equal strings as distinct values, so `('k'..i) == ('k'..i)` went false
 * and table lookups by rebuilt keys silently missed (seen on generic32 at
 * a 96 KB limit; this workload recreates that pressure).
 */
TEST(GC_InternSurvivesResizeOOM) {
  MLuaState *L = NewState();
  ASSERT_NE(L, NULL);

  const char *src = "local s = ''\n"
                    "for i = 1, 400 do s = s .. (i % 10) .. 'y' end\n"
                    "local t = {}\n"
                    "for i = 1, 2000 do t[i] = i * 3 end\n"
                    "local h = {}\n"
                    "local badeq, badhit = 0, 0\n"
                    "for i = 1, 300 do\n"
                    "  h['k' .. i] = i\n"
                    "  if ('k' .. i) ~= ('k' .. i) then badeq = badeq + 1 end\n"
                    "  if h['k' .. i] ~= i then badhit = badhit + 1 end\n"
                    "end\n"
                    "for i = 1, 300 do\n"
                    "  if h['k' .. i] ~= i then badhit = badhit + 1 end\n"
                    "end\n"
                    "result = badeq * 1000 + badhit\n";
  ASSERT(RunAndCheckInt(L, src, 0));
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf("TestCoro:\n");

  RUN_TEST(Coro_YieldOutsideCoroutine);
  RUN_TEST(Coro_ResumeNonThread);
  RUN_TEST(Coro_StatusTransitions);
  RUN_TEST(VM_FrameOverflow);
  RUN_TEST(GC_StringChurnReclaimed);
  RUN_TEST(GC_TableChurnReclaimed);
  RUN_TEST(GC_ClosureChurnSurvivesCompaction);
  RUN_TEST(GC_ClosedUpvalueSurvivesCompaction);
  RUN_TEST(GC_SuspendedCoroutineSurvivesCompaction);
  RUN_TEST(GC_ExplicitCollectWithGCRef);
  RUN_TEST(GC_HeapShrinksAfterChurn);
  RUN_TEST(GC_NoClearAllocSurvivesCompaction);
  RUN_TEST(GC_InternSurvivesResizeOOM);

  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);
  return TestsFailed > 0 ? 1 : 0;
}
