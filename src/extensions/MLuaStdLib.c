/*
 * MicroLua - MLuaStdLib.c
 * OPTIONAL io/os extension.
 *
 * This file links against the C standard library and is therefore NOT part
 * of the freestanding core: it is compiled only into embedders that want it
 * (the bundled REPL does). It provides:
 *
 *   io.write/read/open/close/lines, file:read/write/close/lines
 *   os.time/clock/date/getenv/exit
 *   dofile, loadfile
 *
 * File handles are plain tables carrying a slot index into a process-wide
 * FILE* registry, with their methods stored directly on the table — no
 * metatables required (f:read(...) passes f as self).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../MLuaConvert.h"
#include "../MLuaCore.h"
#include "../MLuaStdLib.h"
#include "../MLuaString.h"
#include "../MLuaTable.h"
#include "../MLuaVM.h"

/* ========================================================================== */
/* FILE* registry                                                             */
/* ========================================================================== */

#define MAX_OPEN_FILES 16

static FILE *OpenFiles[MAX_OPEN_FILES];

static int RegisterFile(FILE *f) {
  int i;
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (!OpenFiles[i]) {
      OpenFiles[i] = f;
      return i;
    }
  }
  return -1;
}

static FILE *HandleFile(MLuaState *L, MLuaValue handle) {
  MLuaValue slot;
  I32 idx;

  if (!IsPtr(handle)) {
    return NULL;
  }
  slot = MLuaTableGet(L, handle, MLuaStringNew(L, "__fd", 4));
  if (!IsInt(slot)) {
    return NULL;
  }
  idx = GetInt(slot);
  if (idx < 0 || idx >= MAX_OPEN_FILES) {
    return NULL;
  }
  return OpenFiles[idx];
}

/* ========================================================================== */
/* Read helpers                                                               */
/* ========================================================================== */

/* Push one read result for a format; returns results pushed (0 on EOF) */
static int ReadFormat(MLuaState *L, FILE *f, const char *fmt) {
  /* Skip a leading '*' (Lua 5.1 style formats) */
  if (fmt && fmt[0] == '*') {
    fmt++;
  }

  if (!fmt || fmt[0] == 'l' || fmt[0] == 'L') {
    /* Line (without/with newline) */
    char buf[1024];
    Size len;
    if (!fgets(buf, sizeof(buf), f)) {
      return 0;
    }
    len = (Size)strlen(buf);
    if (fmt && fmt[0] == 'l' && len > 0 && buf[len - 1] == '\n') {
      len--;
    } else if (!fmt && len > 0 && buf[len - 1] == '\n') {
      len--;
    }
    MLuaPush(L, MLuaStringNew(L, buf, len));
    return 1;
  }

  if (fmt[0] == 'n') {
    double num;
    if (fscanf(f, "%lf", &num) != 1) {
      return 0;
    }
    MLuaPush(L, MLuaMakeNumber(L, num));
    return 1;
  }

  if (fmt[0] == 'a') {
    /* Read everything that remains */
    char chunk[1024];
    char *all = NULL;
    Size total = 0;
    Size got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
      char *grown = realloc(all, total + got);
      if (!grown) {
        free(all);
        return 0;
      }
      all = grown;
      memcpy(all + total, chunk, got);
      total += got;
    }
    MLuaPush(L, MLuaStringNew(L, all ? all : "", total));
    free(all);
    return 1;
  }

  return 0;
}

static const char *FormatArg(MLuaState *L, int index) {
  MLuaValue v = MLuaGetArg(L, index);
  return MLuaStringData(v); /* NULL when not a string */
}

/* ========================================================================== */
/* io.*                                                                       */
/* ========================================================================== */

static int IoWrite(MLuaState *L) {
  int top = MLuaGetTop(L);
  int i;

  for (i = 0; i < top; i++) {
    MLuaValue v = MLuaGetArg(L, i);
    if (IsString(v) || IsShortStr(v)) {
      fwrite(MLuaStringData(v), 1, MLuaStringLen(v), stdout);
    } else {
      char tmp[64];
      Size n = MLuaValueToStr(L, v, tmp, sizeof(tmp));
      fwrite(tmp, 1, n, stdout);
    }
  }
  fflush(stdout);
  return 0;
}

static int IoRead(MLuaState *L) {
  return ReadFormat(L, stdin, MLuaGetTop(L) >= 1 ? FormatArg(L, 0) : NULL);
}

static int FileRead(MLuaState *L) {
  FILE *f = HandleFile(L, MLuaGetArg(L, 0));
  if (!f) {
    L->ErrorMsg = "attempt to use a closed file";
    return -1;
  }
  return ReadFormat(L, f, MLuaGetTop(L) >= 2 ? FormatArg(L, 1) : NULL);
}

static int FileWrite(MLuaState *L) {
  FILE *f = HandleFile(L, MLuaGetArg(L, 0));
  int top = MLuaGetTop(L);
  int i;

  if (!f) {
    L->ErrorMsg = "attempt to use a closed file";
    return -1;
  }
  for (i = 1; i < top; i++) {
    MLuaValue v = MLuaGetArg(L, i);
    if (IsString(v) || IsShortStr(v)) {
      fwrite(MLuaStringData(v), 1, MLuaStringLen(v), f);
    } else {
      char tmp[64];
      Size n = MLuaValueToStr(L, v, tmp, sizeof(tmp));
      fwrite(tmp, 1, n, f);
    }
  }
  /* Return the handle for chaining (f:write(a):write(b)) */
  MLuaPush(L, MLuaGetArg(L, 0));
  return 1;
}

static int FileClose(MLuaState *L) {
  MLuaValue handle = MLuaGetArg(L, 0);
  MLuaValue slot = MLuaTableGet(L, handle, MLuaStringNew(L, "__fd", 4));

  if (IsInt(slot)) {
    I32 idx = GetInt(slot);
    if (idx >= 0 && idx < MAX_OPEN_FILES && OpenFiles[idx]) {
      fclose(OpenFiles[idx]);
      OpenFiles[idx] = NULL;
      MLuaTableSet(L, handle, MLuaStringNew(L, "__fd", 4), MakeInt(-1));
      MLuaPush(L, MLUA_TRUE);
      return 1;
    }
  }
  MLuaPush(L, MLUA_FALSE);
  return 1;
}

/* Iterator for file:lines() / io.lines(): iter(handle) -> line | nil */
static int FileLinesIter(MLuaState *L) {
  FILE *f = HandleFile(L, MLuaGetArg(L, 0));
  char buf[1024];
  Size len;

  if (!f || !fgets(buf, sizeof(buf), f)) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }
  len = (Size)strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    len--;
  }
  MLuaPush(L, MLuaStringNew(L, buf, len));
  return 1;
}

static int FileLines(MLuaState *L) {
  /* Returns (iterator, handle, nil) for the generic for */
  MLuaPush(L, MLuaRegisterCFunc(L, FileLinesIter));
  MLuaPush(L, MLuaGetArg(L, 0));
  MLuaPush(L, MLUA_NIL);
  return 3;
}

static int IoOpen(MLuaState *L) {
  MLuaValue pathv = MLuaGetArg(L, 0);
  MLuaValue modev = MLuaGetArg(L, 1);
  const char *path = MLuaStringData(pathv);
  const char *mode = MLuaStringData(modev);
  char pathBuf[512];
  char modeBuf[8];
  FILE *f;
  int idx;
  MLuaValue handle;

  if (!path) {
    L->ErrorMsg = "bad argument #1 to 'open' (string expected)";
    return -1;
  }
  /* Copy out of the rotating short-string buffers before reuse */
  snprintf(pathBuf, sizeof(pathBuf), "%.*s", (int)MLuaStringLen(pathv), path);
  if (mode) {
    snprintf(modeBuf, sizeof(modeBuf), "%.*s", (int)MLuaStringLen(modev),
             mode);
  } else {
    modeBuf[0] = 'r';
    modeBuf[1] = 0;
  }

  f = fopen(pathBuf, modeBuf);
  if (!f) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "cannot open file", 16));
    return 2;
  }

  idx = RegisterFile(f);
  if (idx < 0) {
    fclose(f);
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "too many open files", 19));
    return 2;
  }

  /* Handle table: slot index + methods as plain fields (no metatables) */
  handle = MLuaTableNewSized(L, 0, 8);
  MLuaTableSet(L, handle, MLuaStringNew(L, "__fd", 4), MakeInt(idx));
  MLuaTableSet(L, handle, MLuaStringNew(L, "read", 4),
               MLuaRegisterCFunc(L, FileRead));
  MLuaTableSet(L, handle, MLuaStringNew(L, "write", 5),
               MLuaRegisterCFunc(L, FileWrite));
  MLuaTableSet(L, handle, MLuaStringNew(L, "close", 5),
               MLuaRegisterCFunc(L, FileClose));
  MLuaTableSet(L, handle, MLuaStringNew(L, "lines", 5),
               MLuaRegisterCFunc(L, FileLines));

  MLuaPush(L, handle);
  return 1;
}

static int IoClose(MLuaState *L) { return FileClose(L); }

/* ========================================================================== */
/* os.*                                                                       */
/* ========================================================================== */

static int OsTime(MLuaState *L) {
  MLuaPush(L, MLuaMakeNumber(L, (double)time(NULL)));
  return 1;
}

static int OsClock(MLuaState *L) {
  MLuaPush(L, MLuaMakeNumber(L, (double)clock() / (double)CLOCKS_PER_SEC));
  return 1;
}

static int OsGetenv(MLuaState *L) {
  const char *name = MLuaStringData(MLuaGetArg(L, 0));
  const char *val = name ? getenv(name) : NULL;

  if (val) {
    MLuaPush(L, MLuaStringNew(L, val, (Size)strlen(val)));
  } else {
    MLuaPush(L, MLUA_NIL);
  }
  return 1;
}

static int OsExit(MLuaState *L) {
  MLuaValue code = MLuaGetArg(L, 0);
  exit(IsInt(code) ? (int)MLuaGetIntVal(code) : 0);
}

static int OsDate(MLuaState *L) {
  const char *fmt = MLuaGetTop(L) >= 1 ? MLuaStringData(MLuaGetArg(L, 0)) : NULL;
  time_t now = time(NULL);
  struct tm *tmv = localtime(&now);
  char buf[128];

  if (fmt && strcmp(fmt, "*t") == 0) {
    MLuaValue t = MLuaTableNewSized(L, 0, 8);
    MLuaTableSet(L, t, MLuaStringNew(L, "year", 4),
                 MakeInt(tmv->tm_year + 1900));
    MLuaTableSet(L, t, MLuaStringNew(L, "month", 5), MakeInt(tmv->tm_mon + 1));
    MLuaTableSet(L, t, MLuaStringNew(L, "day", 3), MakeInt(tmv->tm_mday));
    MLuaTableSet(L, t, MLuaStringNew(L, "hour", 4), MakeInt(tmv->tm_hour));
    MLuaTableSet(L, t, MLuaStringNew(L, "min", 3), MakeInt(tmv->tm_min));
    MLuaTableSet(L, t, MLuaStringNew(L, "sec", 3), MakeInt(tmv->tm_sec));
    MLuaPush(L, t);
    return 1;
  }

  strftime(buf, sizeof(buf), fmt ? fmt : "%c", tmv);
  MLuaPush(L, MLuaStringNew(L, buf, (Size)strlen(buf)));
  return 1;
}

/* ========================================================================== */
/* dofile / loadfile                                                          */
/* ========================================================================== */

#if MLUA_ENABLE_COMPILER
static char *SlurpFile(const char *path, Size *outLen) {
  FILE *f = fopen(path, "rb");
  long size;
  char *buf;

  if (!f) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  fread(buf, 1, (size_t)size, f);
  buf[size] = 0;
  fclose(f);
  *outLen = (Size)size;
  return buf;
}

static int BaseLoadfile(MLuaState *L) {
  MLuaValue pathv = MLuaGetArg(L, 0);
  const char *path = MLuaStringData(pathv);
  char pathBuf[512];
  Size len;
  char *src;
  MLuaStatus st;

  if (!path) {
    L->ErrorMsg = "bad argument #1 to 'loadfile' (string expected)";
    return -1;
  }
  snprintf(pathBuf, sizeof(pathBuf), "%.*s", (int)MLuaStringLen(pathv), path);

  src = SlurpFile(pathBuf, &len);
  if (!src) {
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, MLuaStringNew(L, "cannot open file", 16));
    return 2;
  }

  st = MLuaLoadBuffer(L, src, len, pathBuf);
  free(src);
  if (st != MLUA_OK) {
    /* MLuaLoadBuffer pushed an error message; prepend nil */
    MLuaValue err = MLuaPop(L);
    MLuaPush(L, MLUA_NIL);
    MLuaPush(L, err);
    return 2;
  }
  return 1; /* The compiled chunk is on the stack */
}

static int BaseDofile(MLuaState *L) {
  MLuaValue pathv = MLuaGetArg(L, 0);
  const char *path = MLuaStringData(pathv);
  char pathBuf[512];
  Size len;
  char *src;
  Size before;
  MLuaStatus st;

  if (!path) {
    L->ErrorMsg = "bad argument #1 to 'dofile' (string expected)";
    return -1;
  }
  snprintf(pathBuf, sizeof(pathBuf), "%.*s", (int)MLuaStringLen(pathv), path);

  src = SlurpFile(pathBuf, &len);
  if (!src) {
    L->ErrorMsg = "cannot open file";
    return -1;
  }

  before = L->EvalTop;
  st = MLuaDoBuffer(L, src, len, pathBuf);
  free(src);
  if (st != MLUA_OK) {
    return -1; /* ErrorMsg already set */
  }
  return (int)(L->EvalTop - before); /* Chunk results */
}
#endif

/* ========================================================================== */
/* Registration                                                               */
/* ========================================================================== */

static const MLuaLibEntry IoEntries[] = {{"close", IoClose},
                                         {"lines", FileLines},
                                         {"open", IoOpen},
                                         {"read", IoRead},
                                         {"write", IoWrite},
                                         {NULL, NULL}};

static const MLuaLibEntry OsEntries[] = {{"clock", OsClock},
                                         {"date", OsDate},
                                         {"exit", OsExit},
                                         {"getenv", OsGetenv},
                                         {"time", OsTime},
                                         {NULL, NULL}};

void MLuaOpenStdLib(MLuaState *L) {
  MLuaValue io = MLuaNewLib(L, "io");
  MLuaValue os = MLuaNewLib(L, "os");

  MLuaRegisterLib(L, io, IoEntries);
  MLuaRegisterLib(L, os, OsEntries);

#if MLUA_ENABLE_COMPILER
  MLuaRegisterGlobal(L, "dofile", BaseDofile);
  MLuaRegisterGlobal(L, "loadfile", BaseLoadfile);
#endif
}
