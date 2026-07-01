#include "MLuaAlloc.h"
#include "MLuaGC.h"
#include "MLuaString.h"
#include "MLuaTable.h"

#include <stdio.h>

#define ASSERT_TRUE(cond)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(void) {
  unsigned char heap[64 * 1024] MLUA_ALIGNAS(8);
  MLuaState *L = MLuaNewConstrainedState(heap, sizeof(heap));
  MLuaMemoryStats before;
  MLuaMemoryStats after;
  MLuaValue t;

  ASSERT_TRUE(L != NULL);
  MLuaGetMemoryStats(L, &before);
  ASSERT_TRUE(before.HeapUsed > 0);
  ASSERT_TRUE(before.HeapBaseline == before.HeapUsed);

  t = MLuaTableNew(L);
  ASSERT_TRUE(!IsNil(t));
  ASSERT_TRUE(!IsNil(MLuaStringNew(L, "diagnostic", 10)));
  ASSERT_TRUE(MLuaTableSet(L, t, MakeInt(1), MakeInt(42)));

  MLuaGetMemoryStats(L, &after);
  ASSERT_TRUE(after.HeapUsed >= before.HeapUsed);
  ASSERT_TRUE(after.ObjectCount[OBJTYPE_TABLE] >= 1);
  ASSERT_TRUE(after.ObjectCount[OBJTYPE_STRING] >= 1);
  ASSERT_TRUE(after.ObjectCount[OBJTYPE_RAW] >= 1);
  ASSERT_TRUE(after.TableArrayBytes >= sizeof(MLuaValue));
  ASSERT_TRUE(after.StringPayloadBytes >= 11);

#ifdef MLUA_MEMORY_DIAGNOSTICS
  ASSERT_TRUE(after.AllocCount > before.AllocCount);
  ASSERT_TRUE(after.AllocRequestedBytes > before.AllocRequestedBytes);
#endif

  MLuaGCCollect(L);
  MLuaGetMemoryStats(L, &after);
  ASSERT_TRUE(after.HeapUsed <= after.HeapPeak);

  printf("Memory diagnostics tests passed\n");
  return 0;
}
