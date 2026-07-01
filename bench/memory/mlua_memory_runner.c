#include <stdio.h>
#include <stdlib.h>

#include "MLuaAlloc.h"
#include "MLuaGC.h"
#include "MLuaVM.h"

#define HEAP_BYTES (16 * 1024 * 1024)

static void Output(MLuaState *L, int kind, const char *msg, Size len) {
  FILE *out = kind == MLUA_OUTPUT_ERROR ? stderr : stdout;
  (void)L;
  fwrite(msg, 1, (size_t)len, out);
}

static char *ReadFile(const char *path, Size *outLen) {
  FILE *f = fopen(path, "rb");
  char *buf;
  long len;
  if (!f) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  len = ftell(f);
  if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  buf = (char *)malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    fclose(f);
    return NULL;
  }
  buf[len] = '\0';
  fclose(f);
  *outLen = (Size)len;
  return buf;
}

static void PrintStats(const char *label, const MLuaMemoryStats *s) {
  int i;
  fprintf(stderr,
          "\"%s\":{\"heap_used\":%llu,\"heap_peak\":%llu,"
          "\"heap_total\":%llu,\"heap_baseline\":%llu,"
          "\"alloc_count\":%llu,\"alloc_requested\":%llu,"
          "\"alloc_aligned\":%llu,\"exec_reserved\":%llu,"
          "\"string_table_bytes\":%llu,\"lightfunc_bytes\":%llu,"
          "\"string_payload_bytes\":%llu,\"table_array_bytes\":%llu,"
          "\"table_hash_bytes\":%llu,\"table_inline_array_bytes\":%llu,"
          "\"table_inline_hash_bytes\":%llu,"
          "\"table_external_array_bytes\":%llu,"
          "\"table_external_hash_bytes\":%llu,"
          "\"table_inline_array_count\":%llu,"
          "\"table_inline_hash_count\":%llu,"
          "\"proto_code_bytes\":%llu,"
          "\"proto_constants_bytes\":%llu,\"proto_protos_bytes\":%llu,"
          "\"proto_upvalues_bytes\":%llu,\"proto_lineinfo_bytes\":%llu,"
          "\"proto_linemap_bytes\":%llu,\"object_count\":[",
          label, (unsigned long long)s->HeapUsed,
          (unsigned long long)s->HeapPeak, (unsigned long long)s->HeapTotal,
          (unsigned long long)s->HeapBaseline,
          (unsigned long long)s->AllocCount,
          (unsigned long long)s->AllocRequestedBytes,
          (unsigned long long)s->AllocAlignedBytes,
          (unsigned long long)s->ExecReservedBytes,
          (unsigned long long)s->StringTableBytes,
          (unsigned long long)s->LightFuncBytes,
          (unsigned long long)s->StringPayloadBytes,
          (unsigned long long)s->TableArrayBytes,
          (unsigned long long)s->TableHashBytes,
          (unsigned long long)s->TableInlineArrayBytes,
          (unsigned long long)s->TableInlineHashBytes,
          (unsigned long long)s->TableExternalArrayBytes,
          (unsigned long long)s->TableExternalHashBytes,
          (unsigned long long)s->TableInlineArrayCount,
          (unsigned long long)s->TableInlineHashCount,
          (unsigned long long)s->ProtoCodeBytes,
          (unsigned long long)s->ProtoConstantsBytes,
          (unsigned long long)s->ProtoProtosBytes,
          (unsigned long long)s->ProtoUpvaluesBytes,
          (unsigned long long)s->ProtoLineInfoBytes,
          (unsigned long long)s->ProtoLineMapBytes);
  for (i = 0; i < MLUA_MEMORY_TYPE_SLOTS; i++) {
    fprintf(stderr, "%s%llu", i == 0 ? "" : ",",
            (unsigned long long)s->ObjectCount[i]);
  }
  fprintf(stderr, "],\"object_bytes\":[");
  for (i = 0; i < MLUA_MEMORY_TYPE_SLOTS; i++) {
    fprintf(stderr, "%s%llu", i == 0 ? "" : ",",
            (unsigned long long)s->ObjectBytes[i]);
  }
  fprintf(stderr, "]}");
}

int main(int argc, char **argv) {
  void *heap;
  MLuaState *L;
  char *source;
  Size sourceLen = 0;
  MLuaStatus status;
  MLuaValue func;
  MLuaClosure *cl;
  MLuaMemoryStats afterInit, afterLoad, afterExec, afterGC;

  if (argc != 2) {
    fprintf(stderr, "usage: %s script.lua\n", argv[0]);
    return 2;
  }

  heap = malloc(HEAP_BYTES);
  if (!heap) {
    fprintf(stderr, "heap allocation failed\n");
    return 1;
  }
  L = MLuaNewConstrainedState(heap, HEAP_BYTES);
  if (!L) {
    fprintf(stderr, "state initialization failed\n");
    free(heap);
    return 1;
  }
  MLuaSetOutput(L, Output);
  MLuaOpenLibs(L);
  MLuaGetMemoryStats(L, &afterInit);

  source = ReadFile(argv[1], &sourceLen);
  if (!source) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    free(heap);
    return 1;
  }

  status = MLuaLoadBuffer(L, source, sourceLen, argv[1]);
  free(source);
  MLuaGetMemoryStats(L, &afterLoad);
  if (status != MLUA_OK) {
    fprintf(stderr, "load error: %s\n", L->ErrorMsg ? L->ErrorMsg : "unknown");
    free(heap);
    return 1;
  }

  func = MLuaPop(L);
  cl = MLUA_CLOSURE((MLuaGCHeader *)GetPtr(func));
  status = MLuaExecute(L, cl, 0, 0);
  MLuaGetMemoryStats(L, &afterExec);
  if (status != MLUA_OK) {
    fprintf(stderr, "execute error: %s\n",
            L->ErrorMsg ? L->ErrorMsg : "unknown");
    free(heap);
    return 1;
  }

  MLuaGCCollect(L);
  MLuaGetMemoryStats(L, &afterGC);

  fprintf(stderr, "__MLUA_MEMORY_JSON__ {");
  PrintStats("after_init", &afterInit);
  fprintf(stderr, ",");
  PrintStats("after_load", &afterLoad);
  fprintf(stderr, ",");
  PrintStats("after_execute", &afterExec);
  fprintf(stderr, ",");
  PrintStats("after_gc", &afterGC);
  fprintf(stderr, "}\n");

  free(heap);
  return 0;
}
